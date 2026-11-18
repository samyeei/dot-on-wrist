#pragma once
#if defined(ANNA_LANG_ZH)
#define ANNA_TEXT(en, zh, ja) zh
#elif defined(ANNA_LANG_JA)
#define ANNA_TEXT(en, zh, ja) ja
#else
#define ANNA_TEXT(en, zh, ja) en
#endif
