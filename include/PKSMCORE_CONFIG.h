// PKSM-Core build configuration for PKForge 3DS: strings and personal tables live in RomFS.
#ifndef PKSMCORE_CONFIG_H
#define PKSMCORE_CONFIG_H

#define _PKSMCORE_LANG_FOLDER "romfs:/i18n/"
#define _PKSMCORE_PERSONAL_FOLDER "romfs:/personal/"
// newlib only declares getline under strict POSIX; its own name is always there.
#define _PKSMCORE_GETLINE_FUNC __getline

#endif
