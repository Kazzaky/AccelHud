#include "cpu_support.h"
#include <cpuid.h>
#include <stdio.h>

static cpu_support_info_t cpu_support_info = {0};

static int cpu_supports_sse2(void);
static int cpu_supports_sse41(void);
static int cpu_supports_avx(void);

void init_cpu_support(void)
{
  cpu_support_info.sse2 = cpu_supports_sse2();
  cpu_support_info.sse41 = cpu_supports_sse41();
  cpu_support_info.avx = cpu_supports_avx();
}

static int cpu_supports_sse2(void)
{
  unsigned int eax, ebx, ecx, edx;
  __cpuid(1, eax, ebx, ecx, edx);
  return edx & (1 << 26);
}

static int cpu_supports_sse41(void)
{
  unsigned int eax, ebx, ecx, edx;
  __cpuid(1, eax, ebx, ecx, edx);
  return ebx & (1 << 19);
}

static int cpu_supports_avx(void)
{
  unsigned int eax, ebx, ecx, edx;
  __cpuid(1, eax, ebx, ecx, edx);
  int xsave_xrstor_enabled = ecx & (1 << 27);
  int avx_support = ecx & (1 << 28);

  if (xsave_xrstor_enabled && avx_support) {
    __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    return (eax & 0x6) == 0x6;
  }

  return 0;
}

#if defined(_WIN32)
#include <windows.h>
int get_physical_core_count(void) {
  DWORD len = 0;
  GetLogicalProcessorInformation(NULL, &len);
  if (len == 0) return 1;

  SYSTEM_LOGICAL_PROCESSOR_INFORMATION* buffer =
    (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)malloc(len);
  if (!buffer) return 1;

  if (!GetLogicalProcessorInformation(buffer, &len)) {
    free(buffer);
    return 1;
  }

  DWORD count = 0;
  DWORD elements = len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
  for (DWORD i = 0; i < elements; i++) {
    if (buffer[i].Relationship == RelationProcessorCore) {
      count++;
    }
  }

  free(buffer);
  return (int)(count > 0 ? count : 1);
}

#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
int get_physical_core_count(void) {
  int count;
  size_t size = sizeof(count);
  if (sysctlbyname("hw.physicalcpu", &count, &size, NULL, 0) == 0 && count > 0)
    return count;
  return 1;
}

#elif defined(__linux__)
#include <unistd.h>
#include <string.h>
int get_physical_core_count(void) {
  FILE* fp = fopen("/proc/cpuinfo", "r");
  if (!fp) return 1;

  char line[256];
  int physical_ids[256] = {0};
  int core_ids[256] = {0};
  int unique_cores = 0;

  int physical_id = -1, core_id = -1;

  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "physical id", 11) == 0) {
      sscanf(line, "physical id : %d", &physical_id);
    } else if (strncmp(line, "core id", 7) == 0) {
      sscanf(line, "core id : %d", &core_id);
      if (physical_id >= 0 && core_id >= 0) {
        int seen = 0;
        for (int i = 0; i < unique_cores; i++) {
          if (physical_ids[i] == physical_id && core_ids[i] == core_id) {
            seen = 1;
            break;
          }
        }
        if (!seen) {
          physical_ids[unique_cores] = physical_id;
          core_ids[unique_cores] = core_id;
          unique_cores++;
        }
        physical_id = core_id = -1;
      }
    }
  }
  fclose(fp);
  return (unique_cores > 0) ? unique_cores : 1;
}

#else
int get_physical_core_count() {
  return 1;
}
#endif
