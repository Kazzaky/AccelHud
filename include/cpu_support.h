#ifndef CPU_SUPPORT_H
#define CPU_SUPPORT_H

void init_cpu_support(void);

// int cpu_supports_sse2(void);
// int cpu_supports_sse41(void);
// int cpu_supports_avx(void);

int get_physical_core_count(void);

typedef struct {
	int sse2;
	int sse41;
	int avx;
} cpu_support_info_t;

static cpu_support_info_t cpu_support_info;

#endif // CPU_SUPPORT_H
