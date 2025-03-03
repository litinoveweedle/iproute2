/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * f_fw.c		FW filter.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <linux/if.h> /* IFNAMSIZ */
#include "utils.h"
#include "tc_util.h"

static void explain(void)
{
	fprintf(stderr,
		"Usage: ... fw [ classid CLASSID ] [ indev DEV ] [ action ACTION_SPEC ]\n"
		"	CLASSID := Push matching packets to the class identified by CLASSID with format X:Y\n"
		"		CLASSID is parsed as hexadecimal input.\n"
		"	DEV := specify device for incoming device classification.\n"
		"	ACTION_SPEC := Apply an action on matching packets.\n"
		"	NOTE: handle is represented as HANDLE[/FWMASK].\n"
		"		FWMASK is 0xffffffff by default.\n");
}

static int fw_parse_opt(struct filter_util *qu, char *handle, int argc, char **argv, struct nlmsghdr *n)
{
	struct tcmsg *t = NLMSG_DATA(n);
	struct rtattr *tail;
	__u32 mask = 0;
	int mask_set = 0;

	if (handle) {
		char *slash;

		if ((slash = strchr(handle, '/')) != NULL)
			*slash = '\0';
		if (get_u32(&t->tcm_handle, handle, 0)) {
			fprintf(stderr, "Illegal \"handle\"\n");
			return -1;
		}
		if (slash) {
			if (get_u32(&mask, slash+1, 0)) {
				fprintf(stderr, "Illegal \"handle\" mask\n");
				return -1;
			}
			mask_set = 1;
		}
	}

	if (argc == 0)
		return 0;

	tail = addattr_nest(n, 4096, TCA_OPTIONS);

	if (mask_set)
		addattr32(n, MAX_MSG, TCA_FW_MASK, mask);

	while (argc > 0) {
		if (matches(*argv, "classid") == 0 ||
		    matches(*argv, "flowid") == 0) {
			unsigned int classid;

			NEXT_ARG();
			if (get_tc_classid(&classid, *argv)) {
				fprintf(stderr, "Illegal \"classid\"\n");
				return -1;
			}
			addattr_l(n, 4096, TCA_FW_CLASSID, &classid, 4);
		} else if (matches(*argv, "police") == 0) {
			NEXT_ARG();
			if (parse_police(&argc, &argv, TCA_FW_POLICE, n)) {
				fprintf(stderr, "Illegal \"police\"\n");
				return -1;
			}
			continue;
		} else if (matches(*argv, "action") == 0) {
			NEXT_ARG();
			if (parse_action(&argc, &argv, TCA_FW_ACT, n)) {
				fprintf(stderr, "Illegal fw \"action\"\n");
				return -1;
			}
			continue;
		} else if (strcmp(*argv, "indev") == 0) {
			char d[IFNAMSIZ+1] = {};

			argc--;
			argv++;
			if (argc < 1) {
				fprintf(stderr, "Illegal indev\n");
				return -1;
			}
			strncpy(d, *argv, sizeof(d) - 1);
			addattr_l(n, MAX_MSG, TCA_FW_INDEV, d, strlen(d) + 1);
		} else if (strcmp(*argv, "help") == 0) {
			explain();
			return -1;
		} else {
			fprintf(stderr, "What is \"%s\"?\n", *argv);
			explain();
			return -1;
		}
		argc--; argv++;
	}
	addattr_nest_end(n, tail);
	return 0;
}

static int fw_print_opt(struct filter_util *qu, FILE *f, struct rtattr *opt, __u32 handle)
{
	struct rtattr *tb[TCA_FW_MAX+1];

	if (opt == NULL)
		return 0;

	parse_rtattr_nested(tb, TCA_FW_MAX, opt);

	if (handle || tb[TCA_FW_MASK]) {
		__u32 mark = 0, mask = 0;

		open_json_object("handle");
		if (handle)
			mark = handle;
		if (tb[TCA_FW_MASK] &&
		    (mask = rta_getattr_u32(tb[TCA_FW_MASK])) != 0xFFFFFFFF) {
			print_hex(PRINT_ANY, "mark", "handle 0x%x", mark);
			print_hex(PRINT_ANY, "mask", "/0x%x ", mask);
		} else {
			print_hex(PRINT_ANY, "mark", "handle 0x%x ", mark);
			print_hex(PRINT_JSON, "mask", NULL, 0xFFFFFFFF);
		}
		close_json_object();
	}

	if (tb[TCA_FW_CLASSID]) {
		SPRINT_BUF(b1);
		print_string(PRINT_ANY, "classid", "classid %s ",
			     sprint_tc_classid(
				     rta_getattr_u32(tb[TCA_FW_CLASSID]), b1));
	}

	if (tb[TCA_FW_POLICE])
		tc_print_police(f, tb[TCA_FW_POLICE]);
	if (tb[TCA_FW_INDEV]) {
		struct rtattr *idev = tb[TCA_FW_INDEV];

		print_string(PRINT_ANY, "indev", "input dev %s ",
			     rta_getattr_str(idev));
	}

	if (tb[TCA_FW_ACT]) {
		print_string(PRINT_FP, NULL, "\n", "");
		tc_print_action(f, tb[TCA_FW_ACT], 0);
	}
	return 0;
}

struct filter_util fw_filter_util = {
	.id = "fw",
	.parse_fopt = fw_parse_opt,
	.print_fopt = fw_print_opt,
};
