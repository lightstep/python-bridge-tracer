#include "tracer_bridge.h"

#include <sstream>
#include <exception>

#include "span.h"
#include "span_context.h"
#include "dict_writer.h"
#include "dict_reader.h"
#include "python_bridge_tracer/utility.h"
#include "opentracing_module.h"
#include "python_bridge_tracer/python_object_wrapper.h"
#include "python_bridge_tracer/python_string_wrapper.h"
#include "python_bridge_error.h"

#include "python_bridge_tracer/module.h"

static opentracing::string_view BinaryFormat{"binary"}, TextMapFormat{"text_map"},
      HttpHeadersFormat{"http_headers"};

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
  if (parent == nullptr || parent == Py_None) {
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
  PythonObjectWrapper active_scope = PyObject_GetAttrString(scope_manager, "active");
  if (active_scope.error()) {
    return false;
  }
  if (active_scope == Py_None) {
    return true;
  }
  PythonObjectWrapper active_span = PyObject_GetAttrString(active_scope, "span");
  if (active_span.error()) {
    return false;
  }
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
  if (!isString(reference_type)) {
    PyErr_Format(PyExc_TypeError, "reference_type must be a string");
    return false;
  }
  cpp_reference_type = opentracing::SpanReferenceType::ChildOfRef;
  return true;
  PythonStringWrapper reference_type_str{reference_type};
  if (reference_type_str.error()) {
    return false;
  }
  static opentracing::string_view child_of{"child_of"};
  static opentracing::string_view follows_from{"follows_from"};
  if (reference_type_str == child_of) {
    cpp_reference_type = opentracing::SpanReferenceType::ChildOfRef;
    return true;
  }
  if (reference_type_str == follows_from) {
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
  PythonObjectWrapper reference_type = PyObject_GetAttrString(reference, "type");
  if (reference_type.error()) {
    return false;
  }
  opentracing::SpanReferenceType cpp_reference_type;
  if (!getReferenceType(reference_type, cpp_reference_type)) {
    return false;
  }
  PythonObjectWrapper span_context = PyObject_GetAttrString(reference, "referenced_context");
  if (span_context.error()) {
    return false;
  }
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
// setPropagationError
//--------------------------------------------------------------------------------------------------
static void setPropagationError(std::error_code error_code) noexcept {
  if (error_code == python_error) {
    // error was already set
    return;
  }
  // Note: Use string comparison for error category to work around issues
  // described here with dynamically loaded tracers.
  //
  // https://github.com/envoyproxy/envoy/issues/5481#issuecomment-452229998
  if (error_code.category().name() ==
      opentracing::string_view{"OpenTracingPropagationError"}) {
    if (error_code.value() ==
        opentracing::span_context_corrupted_error.value()) {
      PythonObjectWrapper exception = getSpanContextCorruptedException();
      if (exception.error()) {
        return;
      }
      PyErr_Format(exception, "%s", error_code.message().c_str());
      return;
    }
  }
  PyErr_Format(PyExc_RuntimeError, "%s", error_code.message().c_str());
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
    options.start_system_timestamp = toTimestamp(start_time);
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
  bool was_successful = false;
  if (format == BinaryFormat) {
    was_successful =
        injectBinary(getSpanContext(span_context).span_context(), carrier);
  } else if (format == TextMapFormat) {
    was_successful = inject<opentracing::TextMapWriter>(
        getSpanContext(span_context).span_context(), carrier);
  } else if (format == HttpHeadersFormat) {
    was_successful = inject<opentracing::HTTPHeadersWriter>(
        getSpanContext(span_context).span_context(), carrier);
  } else {
    PythonObjectWrapper exception = getUnsupportedFormatException();
    if (exception.error()) {
      return nullptr;
    }
    PyErr_Format(exception, "unsupported format %s", format.data());
    return nullptr;
  }
  if (!was_successful) {
    return nullptr;
  }
  Py_RETURN_NONE;
}

template <class Carrier>
bool TracerBridge::inject(const opentracing::SpanContext& span_context, PyObject* carrier) noexcept {
  DictWriter dict_writer{carrier};
  auto result = tracer_->Inject(span_context, static_cast<Carrier&>(dict_writer));
  if (!result) {
    setPropagationError(result.error());
    return false;
  }
  return true;
}

//--------------------------------------------------------------------------------------------------
// extract
//--------------------------------------------------------------------------------------------------
PyObject* TracerBridge::extract(PyObject* args, PyObject* keywords) noexcept {
  static char* keyword_names[] = {const_cast<char*>("format"),
                                  const_cast<char*>("carrier"), nullptr};
  const char* format_data = nullptr;
  int format_length = 0;
  PyObject* carrier = nullptr;
  if (PyArg_ParseTupleAndKeywords(args, keywords, "s#O:inject", keyword_names,
                                  &format_data, &format_length,
                                  &carrier) == 0) {
    return nullptr;
  }
  opentracing::string_view format{format_data,
                                  static_cast<size_t>(format_length)};
  opentracing::expected<std::unique_ptr<opentracing::SpanContext>> span_context_maybe;
  if (format == BinaryFormat) {
    span_context_maybe = extractBinary(carrier);
  } else if (format == TextMapFormat) {
    span_context_maybe = extract<opentracing::TextMapReader>(carrier);
  } else if (format == HttpHeadersFormat) {
    span_context_maybe = extract<opentracing::HTTPHeadersReader>(carrier);
  } else {
    PythonObjectWrapper exception = getUnsupportedFormatException();
    if (exception.error()) {
      return nullptr;
    }
    PyErr_Format(exception, "unsupported format %s", format.data());
    return nullptr;
  }
  if (!span_context_maybe) {
    setPropagationError(span_context_maybe.error());
    return nullptr;
  }
  std::unique_ptr<opentracing::SpanContext> span_context{std::move(*span_context_maybe)};
  if (span_context == nullptr) {
    Py_RETURN_NONE;
  }
  std::unique_ptr<SpanContextBridge> span_context_bridge{
      new SpanContextBridge{std::move(span_context)}};
  return makeSpanContext(std::move(span_context_bridge));
}

template <class Carrier>
opentracing::expected<std::unique_ptr<opentracing::SpanContext>>
TracerBridge::extract(PyObject* carrier) noexcept {
  if (PyDict_Check(carrier) != 1) {
    return opentracing::make_unexpected(python_error);
  }
  DictReader dict_reader{carrier};
  return tracer_->Extract(static_cast<Carrier&>(dict_reader));
}

//--------------------------------------------------------------------------------------------------
// injectBinary
//--------------------------------------------------------------------------------------------------
bool TracerBridge::injectBinary(const opentracing::SpanContext& span_context,
                                PyObject* carrier) noexcept {
  if (PyByteArray_Check(carrier) != 1) {
    PythonObjectWrapper exception = getInvalidCarrierException();
    if (exception.error()) {
      return false;
    }
    PyErr_Format(exception, "carrier must be a bytearray");
    return false;
  }
  std::ostringstream oss;
  auto was_successful = tracer_->Inject(span_context, oss);
  if (!was_successful) {
    setPropagationError(was_successful.error());
    return false;
  }
  auto s = oss.str();
  auto size = PyByteArray_Size(carrier);
  if (PyByteArray_Resize(carrier, size + static_cast<Py_ssize_t>(s.size())) != 0) {
    return false;
  }
  auto data = PyByteArray_AsString(carrier);
  std::copy(s.begin(), s.end(), data + size);
  return true;
}

//--------------------------------------------------------------------------------------------------
// extractBinary
//--------------------------------------------------------------------------------------------------
opentracing::expected<std::unique_ptr<opentracing::SpanContext>>
TracerBridge::extractBinary(PyObject* carrier) noexcept {
  if (PyByteArray_Check(carrier) != 1) {
    PythonObjectWrapper exception = getInvalidCarrierException();
    if (exception.error()) {
      return opentracing::make_unexpected(python_error);
    }
    PyErr_Format(exception, "carrier must be a bytearray");
    return opentracing::make_unexpected(python_error);
  }
  auto data = PyByteArray_AsString(carrier);
  auto size = PyByteArray_Size(carrier);
  std::istringstream iss{std::string{data, static_cast<size_t>(size)}};
  return tracer_->Extract(iss);
}
}  // namespace python_bridge_tracer
