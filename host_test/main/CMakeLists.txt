idf_component_register(SRCS "host_test_main.c"
                            "test_version.c"
                            "test_beacon.c"
                            "test_trickle.c"
                            "test_chunker.c"
                            "test_verify.c"
    INCLUDE_DIRS "." REQUIRES hive_core unity)
