#pragma once

#ifndef PYTHON_BRIDGE_TRACER_MODULE
#define PYTHON_BRIDGE_TRACER_MODULE "bridge_tracer"
#endif

#include <Python.h>
#include <opentracing/tracer.h>

#include "python_bridge_tracer/version.h"

#ifdef PYTHON_BRIDGE_TRACER_PY3
#define PYTHON_BRIDGE_TRACER_DEFINE_MODULE(NAME) PyMODINIT_FUNC PyInit_##NAME() noexcept
#define PYTHON_BRIDGE_TRACER_MODULE_RETURN(VALUE) return VALUE
#else
#define PYTHON_BRIDGE_TRACER_DEFINE_MODULE(NAME) PyMODINIT_FUNC init##NAME() noexcept
#define PYTHON_BRIDGE_TRACER_MODULE_RETURN(VALUE) do { (void)(VALUE); return; } while (false)
#endif

namespace python_bridge_tracer {
/**
 * Extract opentracing::Tracer from its python object wrapper
 * @return the extracted opentracing::Tracer
 */
opentracing::Tracer& extractTracer(PyObject* tracer_object) noexcept;

/**
 * Make an OpenTracing python tracer from a C++ tracer and a scope manager
 * @param tracer the C++ tracer
 * @param scope_manager a scope manager object
 * @return the OpenTracing python tracer object
 */
PyObject* makeTracer(std::shared_ptr<opentracing::Tracer> tracer, PyObject* scope_manager) noexcept;

/**
 * An extension method not part of the official OpenTracing API but commonly
 * added. Tracers can implement this function or optionally do nothing if flush
 * isn't supported.
 *
 * @param tracer the tracer
 * @param the flush timeout or 0 if none was provided.
 */
void flush(opentracing::Tracer& tracer, std::chrono::microseconds timeout) noexcept;

/**
 * Setup the OpenTracing python classes.
 * @param module the module to add the classes to
 * @param tracer_extension_methods additional methods a vendor tracer can add
 * @param tracer_extension_getsets additional getsets a vendor tracer can add
 * @return true if successful
 */
bool setupClasses(
    PyObject* module,
    const std::vector<PyMethodDef>& tracer_extension_methods = {},
    const std::vector<PyGetSetDef>& tracer_extension_getsets = {}) noexcept;

PyObject* makeModule(const char* name, const char* doc, PyMethodDef* methods) noexcept;
} // namespace python_bridge_tracer
