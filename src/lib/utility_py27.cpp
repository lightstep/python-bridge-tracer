#include "python_bridge_tracer/utility.h"

#include "python_bridge_tracer/version.h"

#ifndef PYTHON_BRIDGE_TRACER_PY3

namespace python_bridge_tracer {
//--------------------------------------------------------------------------------------------------
// isString
//--------------------------------------------------------------------------------------------------
bool isString(PyObject* obj) noexcept {
  return static_cast<bool>(PyString_Check(obj));
}

//--------------------------------------------------------------------------------------------------
// isInt
//--------------------------------------------------------------------------------------------------
bool isInt(PyObject* obj) noexcept {
  return static_cast<bool>(PyInt_Check(obj));
}

//--------------------------------------------------------------------------------------------------
// toLong
//--------------------------------------------------------------------------------------------------
bool toLong(PyObject* obj, long& value) noexcept {
  value = PyInt_AsLong(obj);
  return !(value == -1 && PyErr_Occurred() != nullptr);
}

//--------------------------------------------------------------------------------------------------
// toPyString
//--------------------------------------------------------------------------------------------------
PyObject* toPyString(opentracing::string_view s) noexcept {
  return PyString_FromStringAndSize(s.data(), static_cast<Py_ssize_t>(s.size()));
}

//--------------------------------------------------------------------------------------------------
// freeSelf
//--------------------------------------------------------------------------------------------------
void freeSelf(PyObject* self) noexcept { Py_TYPE(self)->tp_free(self); }
} // namespace python_bridge_tracer

#endif 
