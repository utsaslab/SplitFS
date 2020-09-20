/* Copyright (c) 2017 Howard Chu @ Symas Corp. */

typedef int (arg_func)(char *optarg);

typedef enum arg_types {
	arg_onoff, arg_int, arg_long, arg_string, arg_float, arg_magic
} arg_types;

typedef struct arg_desc {
	const char *ad_name;
	arg_types ad_arg_type;
	void *ad_arg_ptr;
} arg_desc;

int arg_setup(arg_desc *main_args, arg_desc *more_args);

int arg_process(int argc, char *argv[]);
