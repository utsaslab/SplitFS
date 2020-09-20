/* Copyright (c) 2017 Howard Chu @ Symas Corp. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#define _GNU_SOURCE
#include <getopt.h>
#include "args.h"

static struct option *our_opts;
static arg_desc *our_args;
static int our_nargs;

int arg_setup(arg_desc *main_args, arg_desc *more_args)
{
	int i, j;
	int num_args;

	if (!main_args)
		return -1;

	for (i=0; main_args[i].ad_name; i++) ;
	num_args = i;

	if (more_args)
	{
		for (i=0; more_args[i].ad_name; i++) ;
		num_args += i;
		our_args = malloc(num_args * sizeof(arg_desc));
		for (i=0; main_args[i].ad_name; i++)
			our_args[i] = main_args[i];
		for (j=0; more_args[j].ad_name; j++, i++)
			our_args[i] = more_args[j];
	}
	our_opts = malloc((num_args+1)*sizeof(struct option));
	for (i=0; i<num_args; i++)
	{
		our_opts[i].name = our_args[i].ad_name;
		our_opts[i].has_arg = required_argument;
		our_opts[i].flag = NULL;
		our_opts[i].val = 0;
	}
	memset(&our_opts[i], 0, sizeof(struct option));
	our_nargs = num_args;
}

int arg_process(int argc, char *argv[])
{
	int opt_index;
	while(1) {
		int opt;
		opt_index = 0;
		opt = getopt_long(argc, argv, "", our_opts, &opt_index);
		if (opt == -1)
			break;
		if (opt == '?') {
			fprintf(stderr, "unknown option %s\n", argv[optind]);
			return -1;
		}
		
		switch(our_args[opt_index].ad_arg_type) {
		case arg_onoff: {
			int val;
			if (sscanf(optarg, "%d", &val) != 1)
				goto garbage;
			if (val == 0 || val == 1)
				*(int *)our_args[opt_index].ad_arg_ptr = val;
			else 
				goto garbage;
			break;
			}
		case arg_int: {
			int val;
			if (sscanf(optarg, "%d", &val) != 1)
				goto garbage;
			*(int *)our_args[opt_index].ad_arg_ptr = val;
			break;
			}
		case arg_long: {
			int64_t val;
			if (sscanf(optarg, "%"SCNd64, &val) != 1)
				goto garbage;
			*(int64_t *)our_args[opt_index].ad_arg_ptr = val;
			break;
			}
		case arg_string:
			*(char **)our_args[opt_index].ad_arg_ptr = optarg;
			break;
		case arg_float: {
			float val;
			if (sscanf(optarg, "%f", &val) != 1)
				goto garbage;
			*(float *)our_args[opt_index].ad_arg_ptr = val;
			break;
			}
		case arg_magic: {
			int ret;
			arg_func *func = (arg_func *)our_args[opt_index].ad_arg_ptr;
			ret = func(optarg);
			if (ret == -1)
				goto garbage;
			if (ret == 1)
				return 1;
			break;
			}
		}
	}
	return 0;
garbage:
	fprintf(stderr, "garbage argument \"%s\" for option %s\n", optarg, our_opts[opt_index].name);
	return -1;
}
