#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

// Print execution time of each test when executed in verbose mode
#define UNITY_INCLUDE_EXEC_TIME

// Call a custom Abort function containing a breakpoint
#ifdef TEST
#define UNITY_TEST_ABORT()  do { \
                              extern struct UNITY_STORAGE_T Unity; \
                              extern void _custom_test_abort(void); \
                              _custom_test_abort(); \
                              longjmp(Unity.AbortFrame, 1); \
                            } while(0)
#endif

#endif /* UNITY_CONFIG_H */