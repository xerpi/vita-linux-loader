#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <string.h>

#define LOG_PATH "ux0:dump/"
#define LOG_FILE LOG_PATH "linuxloader_log.txt"

void log_reset();
void log_write(const char *buffer, size_t length);

#ifndef RELEASE
#  define LOG(...) \
	do { \
		char buffer[256]; \
		snprintf(buffer, sizeof(buffer), ##__VA_ARGS__); \
		log_write(buffer, strlen(buffer)); \
	} while (0)
#else
#  define LOG(...) (void)0
#endif

#define TEST_CALL(f, ...) ({ \
	int ret = f(__VA_ARGS__); \
	LOG(# f " returned 0x%08X\n", ret); \
	ret; \
})

#define LOG_PADDR(vaddr) LOG("paddr(" #vaddr "): 0x%08lX\n", get_paddr(vaddr));

#endif
