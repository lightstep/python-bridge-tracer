#include "tracer_bridge.h"

#include "span.h"
#include "span_context.h"
#include "utility.h"
#include "opentracing_module.h"

#include "python_bridge_tracer/module.h"

namespace python_bridge_tracer {
//--------------------------------------------------------------------------------------------------
// getNumReferences
//--------------------------------------------------------------------------------------------------
static bool getNumReferences(PyObject* references,
                             int& num_references) noexcept {
  if (references == nullptr || references == Py_None) {
    num_references = 0;
    return true;
  }
  if (PyList_Check(references) == 0) {
    PyErr_Format(PyExc_TypeError, "references must be a list");
    return false;
  }
  num_references = PyList_Size(references);
  return true;
}

//--------------------------------------------------------------------------------------------------
// addParentReference
//--------------------------------------------------------------------------------------------------
static bool addParentReference(
    PyObject* parent,
    std::vector<std::pair<opentracing::SpanReferenceType, SpanContextBridge>>&
        cpp_references) noexcept {
  if (parent == nullptr) {
    return true;
  }
  if (isSpanContext(parent)) {
    cpp_references.emplace_back(opentracing::SpanReferenceType::ChildOfRef,
                                getSpanContext(parent));
    return true;
  }
  if (isSpan(parent)) {
    cpp_references.emplace_back(opentracing::SpanReferenceType::ChildOfRef,
                                getSpanContextFromSpan(parent));
    return true;
  }
  PyErr_Format(PyExc_TypeError,
               "child_of must be either a " PYTHON_BRIDGE_TRACER_MODULE
               "._SpanContext or a " PYTHON_BRIDGE_TRACER_MODULE "._Span");
  return false;
}

//--------------------------------------------------------------------------------------------------
// addActiveSpanReference
//--------------------------------------------------------------------------------------------------
static bool addActiveSpanReference(
    PyObject* scope_manager,
    std::vector<std::pair<opentracing::SpanReferenceType, SpanContextBridge>>&
        cpp_references) noexcept {
  auto active_scope = PyObject_GetAttrString(scope_manager, "active");
  if (active_scope == nullptr) {
    return false;
  }
  auto cleanup_active_scope =
      finally([active_scope] { Py_DECREF(active_scope); });
  if (active_scope == Py_None) {
    return true;
  }
  auto active_span = PyObject_GetAttrString(active_scope, "span");
  if (active_span == nullptr) {
    return false;
  }
  auto cleanup_active_span = finally([active_span] { Py_DECREF(active_span); });
  if (!isSpan(active_span)) {
    PyErr_Format(
        PyExc_TypeError,
        "unexpected type for active span: expected " PYTHON_BRIDGE_TRACER_MODULE
        "._Span");
    return false;
  }
  cpp_references.emplace_back(opentracing::SpanReferenceType::ChildOfRef,
                              getSpanContextFromSpan(active_span));
  return true;
}

//--------------------------------------------------------------------------------------------------
// getReferenceType
//--------------------------------------------------------------------------------------------------
static bool getReferenceType(
    PyObject* reference_type,
    opentracing::SpanReferenceType& cpp_reference_type) noexcept {
  if (PyUnicode_Check(reference_type) == 0) {
    PyErr_Format(PyExc_TypeError, "reference_type must be a string");
    return false;
  }
  auto utf8 = PyUnicode_AsUTF8String(reference_type);
  if (utf8 == nullptr) {
    return false;
  }
  auto cleanup_utf8 = finally([utf8] { Py_DECREF(utf8); });
  char* s;
  Py_ssize_t length;
  auto rcode = PyBytes_AsStringAndSize(utf8, &s, &length);
  if (rcode == -1) {
    return false;
  }
  auto string_view = opentracing::string_view{s, static_cast<size_t>(length)};
  static opentracing::string_view child_of{"child_of"};
  static opentracing::string_view follows_from{"follows_from"};
  if (string_view == child_of) {
    cpp_reference_type = opentracing::SpanReferenceType::ChildOfRef;
    return true;
  }
  if (string_view == follows_from) {
    cpp_reference_type = opentracing::SpanReferenceType::FollowsFromRef;
    return true;
  }
  PyErr_Format(PyExc_RuntimeError,
               "reference_type must be either 'child_of' or 'follows_from'");
  return false;
}

//--------------------------------------------------------------------------------------------------
// addReference
//--------------------------------------------------------------------------------------------------
static bool addReference(
    PyObject* reference,
    std::vector<std::pair<opentracing::SpanReferenceType, SpanContextBridge>>&
        cpp_references) noexcept {
  auto reference_type = PyObject_GetAttrString(reference, "type");
  if (reference_type == nullptr) {
    return false;
  }
  auto cleanup_reference_type =
      finally([reference_type] { Py_DECREF(reference_type); });
  opentracing::SpanReferenceType cpp_reference_type;
  if (!getReferenceType(reference_type, cpp_reference_type)) {
    return false;
  }
  auto span_context = PyObject_GetAttrString(reference, "referenced_context");
  if (span_context == nullptr) {
    return false;
  }
  auto cleanup_span_context =
      finally([span_context] { Py_DECREF(span_context); });
  if (!isSpanContext(span_context)) {
    PyErr_Format(PyExc_TypeError,
                 "unexpected type for referenced_context: "
                 "expected " PYTHON_BRIDGE_TRACER_MODULE "._SpanContext");
    return false;
  }
  cpp_references.emplace_back(cpp_reference_type, getSpanContext(span_context));
  return true;
}

//--------------------------------------------------------------------------------------------------
// addReferences
//--------------------------------------------------------------------------------------------------
static bool addReferences(
    PyObject* references, int num_references,
    std::vector<std::pair<opentracing::SpanReferenceType, SpanContextBridge>>&
        cpp_references) noexcept {
  for (int i = 0; i < num_references; ++i) {
    auto reference = PyList_GetItem(references, i);
    if (!addReference(reference, cpp_references)) {
      return false;
    }
  }
  return true;
}

//--------------------------------------------------------------------------------------------------
// getCppReferences
//--------------------------------------------------------------------------------------------------
static bool getCppReferences(
    PyObject* scope_manager, PyObject* parent, PyObject* references,
    bool ignore_active_span,
    std::vector<std::pair<opentracing::SpanReferenceType, SpanContextBridge>>&
        cpp_references) noexcept {
  int num_references;
  if (!getNumReferences(references, num_references)) {
    return false;
  }
  cpp_references.reserve(static_cast<size_t>(num_references) + 2);
  if (!addParentReference(parent, cpp_references)) {
    return false;
  }
  if (!ignore_active_span) {
    if (!addActiveSpanReference(scope_manager, cpp_references)) {
      return false;
    }
  }
  return addReferences(references, num_references, cpp_references);
}

//--------------------------------------------------------------------------------------------------
// setTags
//--------------------------------------------------------------------------------------------------
static bool setTags(SpanBridge& span_bridge, PyObject* tags) noexcept {
  if (tags == nullptr || tags == Py_None) {
    return true;
  }
  if (PyDict_Check(tags) == 0) {
    PyErr_Format(PyExc_TypeError, "tags must be a dict");
    return false;
  }
  PyObject* key;
  PyObject* value;
  Py_ssize_t position = 0;
  while (PyDict_Next(tags, &position, &key, &value) == 1) {
    if (!span_bridge.setTagKeyValue(key, value)) {
      return false;
    }
  }
  return true;
}

//--------------------------------------------------------------------------------------------------
// constructor
//--------------------------------------------------------------------------------------------------
TracerBridge::TracerBridge(std::shared_ptr<opentracing::Tracer> tracer) noexcept
    : tracer_{std::move(tracer)} {}

//--------------------------------------------------------------------------------------------------
// makeSpan
//--------------------------------------------------------------------------------------------------
std::unique_ptr<SpanBridge> TracerBridge::makeSpan(
    opentracing::string_view operation_name, PyObject* scope_manager,
    PyObject* parent, PyObject* references, PyObject* tags, double start_time,
    bool ignore_active_span) noexcept {
  std::vector<std::pair<opentracing::SpanReferenceType, SpanContextBridge>>
      cpp_references;
  if (!getCppReferences(scope_manager, parent, references, ignore_active_span,
                        cpp_references)) {
    return nullptr;
  }
  opentracing::StartSpanOptions options;
  options.references.reserve(cpp_references.size());
  for (auto& reference : cpp_references) {
    options.references.emplace_back(reference.first,
                                    &reference.second.span_context());
  }
  if (start_time != 0) {
    auto time_since_epoch =
        std::chrono::nanoseconds{static_cast<uint64_t>(1e9 * start_time)};
    options.start_system_timestamp = std::chrono::system_clock::time_point{
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            time_since_epoch)};
  }

  auto span = tracer_->StartSpanWithOptions(operation_name, options);
  std::unique_ptr<SpanBridge> span_bridge{new SpanBridge{std::move(span)}};
  if (!setTags(*span_bridge, tags)) {
    return nullptr;
  }
  return span_bridge;
}

//--------------------------------------------------------------------------------------------------
// inject
//--------------------------------------------------------------------------------------------------
PyObject* TracerBridge::inject(PyObject* args, PyObject* keywords) noexcept {
  static char* keyword_names[] = {
    const_cast<char*>("span_context"), 
    const_cast<char*>("format"), 
    const_cast<char*>("carrier"), 
    nullptr};
  PyObject* span_context = nullptr;
  const char* format_data = nullptr;
  int format_length = 0;
  PyObject* carrier = nullptr;
  if (PyArg_ParseTupleAndKeywords(args, keywords, "Os#O:inject", keyword_names,
                                  &span_context, &format_data, &format_length,
                                  &carrier) == 0) {
    return nullptr;
  }
  if (!isSpanContext(span_context)) {
    PyErr_Format(PyExc_TypeError,
                 "span_context must be a " PYTHON_BRIDGE_TRACER_MODULE
                 "._SpanContext");
    return nullptr;
  }
  opentracing::string_view format{format_data, static_cast<size_t>(format_length)};
  static opentracing::string_view binary{"binary"}, text_map{"text_map"},
      http_headers{"http_headers"};
  bool was_successful = false;
  if (format == binary) {
    was_successful =
        injectBinary(getSpanContext(span_context).span_context(), carrier);
  } else if (format == text_map) {
    was_successful =
        injectTextMap(getSpanContext(span_context).span_context(), carrier);
  } else if (format == http_headers) {
    was_successful =
        injectHttpHeaders(getSpanContext(span_context).span_context(), carrier);
  } else {
    auto exception = getUnsupportedFormatException();
    if (exception == nullptr) {
      return nullptr;
    }
    auto cleanup_exception = finally([exception] { Py_DECREF(exception); });
    PyErr_Format(exception, "unsupported format %s", format.data());
    return nullptr;
  }
  if (!was_successful) {
    return nullptr;
  }
  Py_RETURN_NONE;
}

//--------------------------------------------------------------------------------------------------
// injectBinary
//--------------------------------------------------------------------------------------------------
bool TracerBridge::injectBinary(const opentracing::SpanContext& span_context,
                                PyObject* carrier) noexcept {
  (void)span_context;
  (void)carrier;
  return true;
}

//--------------------------------------------------------------------------------------------------
// injectTextMap
//--------------------------------------------------------------------------------------------------
bool TracerBridge::injectTextMap(const opentracing::SpanContext& span_context, PyObject* carrier) noexcept {
  (void)span_context;
  (void)carrier;
  return true;
}

bool TracerBridge::injectHttpHeaders(const opentracing::SpanContext& span_context, PyObject* carrier) noexcept {
  (void)span_context;
  (void)carrier;
  return true;
}
}  // namespace python_bridge_tracer
