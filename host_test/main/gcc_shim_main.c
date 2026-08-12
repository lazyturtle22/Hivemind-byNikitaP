/* Entry point for the local gcc test runner (test_scripts/run_host_tests_gcc.ps1).
 * Not part of the IDF build — main/CMakeLists.txt lists SRCS explicitly. */
void app_main(void);

int main(void) {
    app_main();  /* app_main exits non-zero itself on test failures */
    return 0;
}
