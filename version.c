/*
 * PuTTY version numbering
 */

#define STR1(x) #x
#define STR(x) STR1(x)

#if defined SNAPSHOT

char ver[] = "Development snapshot " STR(SNAPSHOT);

#elif defined RELEASE

char ver[] = "Release " STR(RELEASE);

#else

char ver[] = "Unidentified build, " __DATE__ " " __TIME__;

#endif
