/* Copyright (c) 2017 Howard Chu @ Symas Corp. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <limits.h>
#include "dbb.h"

struct Hstctx {
	unsigned int us[1000];
	unsigned int ms[1000];
	unsigned int ss[1000];
	unsigned int min;
	unsigned int max;
	unsigned long num;
	unsigned long sum;
	double sumsq;
};

Hstctx *DBB_hstctx() {
	return malloc(sizeof(Hstctx));
}

void DBB_hstinit(Hstctx *ctx) {
	memset(ctx, 0, sizeof(*ctx));
}

void DBB_hstadd(Hstctx *ctx, struct timeval *tv) {
	int i;
	if (tv->tv_sec) {
		i = tv->tv_sec;
		if (i > 999) i = 999;
		ctx->ss[i]++;
	} else if (tv->tv_usec < 1000) {
		ctx->us[tv->tv_usec]++;
	} else {
		i = tv->tv_usec / 1000;
		ctx->ms[i]++;
	}
}

void DBB_hstmerge(Hstctx *dst, Hstctx *src) {
	int i;
	for (i=0; i<1000; i++) {
		if (src->us[i])
			dst->us[i] += src->us[i];
		if (src->ms[i])
			dst->ms[i] += src->ms[i];
		if (src->ss[i])
			dst->ss[i] += src->ss[i];
	}
}

void DBB_hstsum(Hstctx *ctx) {
	unsigned long num = 0, sum = 0;
	int i, j, scale;
	unsigned int min = UINT_MAX, max = 0, *ptr;
	double sumsq = 0.0;

	i = 0;
	ptr = ctx->us;
	scale = 1;
	for (j=0; j<3000; j++,i++) {
		if (j == 1000) {
			ptr = ctx->ms;
			scale = 1000;
			i = 0;
		} else if (j == 2000) {
			ptr = ctx->ss;
			scale = 1000000;
			i = 0;
		}
		if (ptr[i]) {
			unsigned long l = i * scale, m;
			num += ptr[i];
			m = ptr[i] * l;
			sum += m;
			m *= l;
			sumsq += m;
			if (min > l)
				min = l;
			max = l;
		}
	}

	if (!num) return;
	ctx->min = min;
	ctx->max = max;
	ctx->num = num;
	ctx->sum = sum;
	ctx->sumsq = sumsq;
}


double DBB_hstpct(Hstctx *ctx, double p) {
	unsigned long threshold, sum;
	unsigned int i, j, scale, *ptr;
	if (!ctx->num)
		return 0;

	threshold = ctx->num * (p / 100.0);
	sum = 0;

	i = 0;
	scale = 1;
	ptr = ctx->us;
	for (j=0; j<3000; j++,i++) {
		if (j == 1000) {
			ptr = ctx->ms;
			scale = 1000;
			i = 0;
		} else if (j == 2000) {
			ptr = ctx->ss;
			scale = 1000000;
			i = 0;
		}
		sum += ptr[i];
		if (sum >= threshold) {
			/* Scale linearly within this bucket */
			unsigned long lsum = sum - ptr[i];
			double pos = (double)(threshold - lsum) / ptr[i];

			return (pos + i) * scale;
		}
	}
	return ctx->max;
}

double DBB_hstmedian(Hstctx *ctx) {
	return DBB_hstpct(ctx, 50.0);
}

double DBB_hstmean(Hstctx *ctx) {
	if (!ctx->num) return 0;
	return (double)ctx->sum / ctx->num;
}

double DBB_hstsdev(Hstctx *ctx) {
	double variance;
	if (!ctx->num) return 0;
	variance = (ctx->sumsq * ctx->num - ctx->sum * ctx->sum) / (ctx->num * ctx->num);
	return sqrt(variance);
}

void DBB_hstprint(Hstctx *ctx) {
	double mult;
	unsigned long sum = 0;
	unsigned int i, j, scale, marks, *ptr;

	if (!ctx->num) DBB_hstsum(ctx);
	printf(
		"Count: %lu  Average: %.4f  StdDev: %.2f\n",
		ctx->num, DBB_hstmean(ctx), DBB_hstsdev(ctx));
	printf(
		"Min: %u  Median: %.4f  Max: %u\n",
		ctx->min, DBB_hstmedian(ctx), ctx->max);
	printf("------------------------------------------------------\n");
	mult = 100.0 / ctx->num;

	i = 0;
	scale = 1;
	ptr = ctx->us;
	for (j=0; j<3000; j++,i++) {
		if (j == 1000) {
			ptr = ctx->ms;
			scale = 1000;
			i = 0;
		} else if (j == 2000) {
			ptr = ctx->ss;
			scale = 1000000;
			i = 0;
		}
		if (!ptr[i]) continue;
		sum += ptr[i];
		printf("%7u %9u %7.3f%% %7.3f%% ",
			i*scale,		/* value */
			ptr[i],			/* count */
			mult * ptr[i],	/* percentage */
			mult * sum);	/* cumulative percentage */

		/* Add hash marks based on percentage; 20 marks for 100%. */
		marks = 20.0 * ptr[i] / ctx->num + 0.5;
		for (;marks > 0; marks--) {
			putchar('#');
		}
		putchar('\n');
	}
}
