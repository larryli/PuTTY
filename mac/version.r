/*
 * Current PuTTY version number.  Minor is in BCD
 */
#define VERSION_MAJOR 0x00
#define VERSION_MINOR 0x57

resource 'vers' (1, purgeable) {
#ifdef RELEASE
    VERSION_MAJOR, VERSION_MINOR,
    beta,
#else
    VERSION_MAJOR, VERSION_MINOR + 1,
    development,
#endif
    0, /* No prerelease version */
    verBritain,
#ifdef RELEASESTR
    RELEASESTR,
    "Release " RELEASESTR,
#else
#ifdef SNAPSHOTSTR
    SNAPSHOTSTR,
    "Development snapshot " SNAPSHOTSTR,
#else
    "unknown",
    "Unidentified build, " $$Date " " $$Time,
#endif
#endif
};

resource 'vers' (2, purgeable) {
#ifdef RELEASE
    VERSION_MAJOR, VERSION_MINOR,
    beta,
#else
    VERSION_MAJOR, VERSION_MINOR + 1,
    development,
#endif
    0, /* No prerelease version */
    verBritain,
#ifdef RELEASESTR
    RELEASESTR,
    "PuTTY " RELEASESTR,
#else
#ifdef SNAPSHOTSTR
    SNAPSHOTSTR,
    "PuTTY snapshot " SNAPSHOTSTR,
#else
    "unknown",
    "PuTTY",
#endif
#endif
};

