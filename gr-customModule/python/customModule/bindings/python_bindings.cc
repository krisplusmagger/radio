/*
 * Copyright 2020 Free Software Foundation, Inc.
 *
 * This file is part of GNU Radio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include <pybind11/pybind11.h>

#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/arrayobject.h>

namespace py = pybind11;

// Headers for binding functions
/**************************************/
// The following comment block is used for
// gr_modtool to insert function prototypes
// Please do not delete
/**************************************/
// BINDING_FUNCTION_PROTOTYPES(
    void bind_getTaps(py::module& m);
    void bind_getCFO(py::module& m);
    void bind_getSyncsymbol(py::module& m);
    void bind_getSignal(py::module& m);
    void bind_getEqsignal(py::module& m);
    void bind_getPayload(py::module& m);
    void bind_getEqlizedsig(py::module& m);
    void bind_recordBaseband(py::module& m);
    void bind_getBaseband(py::module& m);
    void bind_Serlizsig(py::module& m);
// ) END BINDING_FUNCTION_PROTOTYPES


// We need this hack because import_array() returns NULL
// for newer Python versions.
// This function is also necessary because it ensures access to the C API
// and removes a warning.
void* init_numpy()
{
    import_array();
    return NULL;
}

PYBIND11_MODULE(customModule_python, m)
{
    // Initialize the numpy C API
    // (otherwise we will see segmentation faults)
    init_numpy();

    // Allow access to base block methods
    py::module::import("gnuradio.gr");

    /**************************************/
    // The following comment block is used for
    // gr_modtool to insert binding function calls
    // Please do not delete
    /**************************************/
    // BINDING_FUNCTION_CALLS(
    bind_getTaps(m);
    bind_getCFO(m);
    bind_getSyncsymbol(m);
    bind_getSignal(m);
    bind_getEqsignal(m);
    bind_getPayload(m);
    bind_getEqlizedsig(m);
    bind_recordBaseband(m);
    bind_getBaseband(m);
    bind_Serlizsig(m);
    // ) END BINDING_FUNCTION_CALLS
}