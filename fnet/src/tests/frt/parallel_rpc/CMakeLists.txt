# Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
vespa_add_executable(fnet_parallel_rpc_test_app TEST
    SOURCES
    parallel_rpc_test.cpp
    DEPENDS
    fnet
)
vespa_add_test(NAME fnet_parallel_rpc_test_app COMMAND fnet_parallel_rpc_test_app)
