#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <paths.h>
#include <errno.h>

#define DEFAULT_COUNT 10
static int DEVNULL;

/* I don't like the global variables, but in this case I don't like 
   to introduce unnecessary argument to output functions. */
static unsigned series = 0;


struct slave_t {
    char **args;
    char *ifile;
    char *iprog;
    char *dir;
};

struct measure_t {
    struct rusage ru;
    struct timeval real;
    int exit_status;
    int bad;
};

typedef long long val_to_long(struct measure_t ru, size_t offset);

typedef void print_function_t(FILE *out, const char *header, int full,
    const struct measure_t *, unsigned count, unsigned bad);

struct output_t {
    FILE *out;
    int filter;
    int print_full;
    int print_calibrate;
    unsigned count;
    unsigned series;
    unsigned do_calibrate;
    print_function_t *print_routine;

};

void help(FILE *out, const char *progname) {
    fprintf(out,
    "usage: %s [-n count] [-i ...] program-to-run\n"
    "   runs program count times and prints min, average and maximum times\n"
    "Options:\n"
    "   -i file --- use file for stdin instead of /dev/null /input/\n"
    "   -p prog --- use command output instead of /dev/null\n"
    "   -n N    --- run program N times /num/\n"
    "   -e      --- easy mode\n"
    "   -b      --- output in sh format /bash/\n"
    "   -C      --- output calibration time\n"
    "   -l      --- print rusage information /long/\n"
    "   -f      --- do not filter result /filter/\n"
    "   -d dir  --- change directory before execute the program\n"
    "   -s N    --- bench by series\n"
    "\n",
                 progname);
}

void batch_print(FILE *out, const char *header, int full,
        const struct measure_t *measures, unsigned count, unsigned bad);

void series_print(FILE *out, const char *header, int full,
        const struct measure_t *measures, unsigned count, unsigned bad);

void normal_print(FILE *out, const char *header, int full,
        const struct measure_t *measures, unsigned count, unsigned bad);

void easy_mode(FILE *out, const char *header, int full,
        const struct measure_t *measures, unsigned count, unsigned bad);

int parse_args(int argc, char **argv, struct output_t *output, struct slave_t *for_test) {
    int opt;
    int i;
    int is_batch;
    memset(output, 0, sizeof(struct output_t));
    output->count = DEFAULT_COUNT;
    output->out = stdout;
    output->print_routine = normal_print;

    memset(for_test, 0, sizeof(struct slave_t));
    for_test->args = calloc(argc, sizeof(char *));

    /* read options */
    setenv("POSIXLY_CORRECT", "1", 0); /* gnu fix */
    while ((opt = getopt(argc, argv, "n:i:p:C:s:hbcfle")) != -1) {
        switch (opt) {
          case 'h':
            help(stdout, argv[0]);
            exit(0);
          case 'n':
            /* read count */
            output->count = atoi(optarg);
            break;
          case 'f':
            output->filter = 0;
            break;
          case 'i':
            if (for_test->iprog) {
                fprintf(stderr, "%s: -i and -p doesn't have sense\n",
                    argv[0]);
                return 1;
            }
            for_test->ifile = strdup(optarg);
            break;
          case 'p':
            if (for_test->ifile) {
                fprintf(stderr, "%s: -i and -p doesn't have sense\n",
                    argv[0]);
                return 1;
            }
            for_test->iprog = strdup(optarg);
            break;
          case 'b':
            output->print_routine = batch_print;
            is_batch = 1;
            break;
          case 'e':
            if (is_batch) {
                fprintf(stderr, 
                    "%s: batch output for easy isn't implemented yet\n",
                    argv[0]);
                return 1;
            }
            output->do_calibrate = 0;
            output->filter = 0;
            output->print_routine = easy_mode;
            break;
          case 's':
            if (is_batch) {
                fprintf(stderr, 
                    "%s: batch output for series isn't implemented yet\n",
                    argv[0]);
                return 1;
            }
            output->series = atoi(optarg);
            if (output->series <= 0) {
                fprintf(stderr, "%s: series count should be greater than 0\n",
                    argv[0]);
                return 1;
            }
            output->filter = 0;
            output->print_routine = series_print;
            break;
          case 'c':
            output->print_calibrate = 1;
            break;
          case 'l':
            output->print_full = 1;
            break;
          case 'C':
            for_test->dir = strdup(optarg);
            break;
          case '?':
          default:
            fprintf(stderr,
                "usage: %s [-n count] [-i input-file] program-to-run\n"
                "error: '%c' option is unknown\n",
                argv[0], optopt);
            return 1;
        }
    }
    if (optind >= argc) {
        fprintf(stderr, "%s: expect some program to execute\n", argv[0]);
        return 1;
    }
    if (output->series != 0) {
        output->count *= output->series;
    }
    for (i = optind; i < argc; ++i) {
        for_test->args[i - optind] = argv[i];
    }
    return 0;
}

int run_program(struct slave_t ft, struct measure_t *measure) {
    pid_t child;
    struct timeval before, after;
    gettimeofday(&before, NULL);
    if ((child = fork()) == 0) {
        int fd;
        /* child */
        dup2(DEVNULL, STDIN_FILENO);
        if (ft.ifile) {
            fd = open(ft.ifile, O_RDONLY);
            if (fd == -1) {
                fprintf(stderr, "can't open input file '%s': %s\n",
                    ft.ifile, strerror(errno));
                exit(1);
            }
            dup2(fd, STDIN_FILENO);
        }
        if (ft.iprog) {
            FILE *p = popen(ft.iprog, "r");
            if (p == 0) {
                fprintf(stderr, "can't run input program '%s': %s\n",
                    ft.iprog, strerror(errno));
                exit(1);
            }
            dup2(fileno(p), STDIN_FILENO);
        }
        if (ft.dir) {
            if (chdir(ft.dir) != 0) {
                fprintf(stderr, "can't change directory to '%s': %s\n",
                    ft.dir, strerror(errno));
                exit(1);
            }
        }
        if (DEVNULL != -1) {
            dup2(DEVNULL, STDOUT_FILENO);
            dup2(DEVNULL, STDERR_FILENO);
        }
        execvp(ft.args[0], ft.args);
        exit(1);
    }
    if (child < 0) {
        /* can't fork */
        return -1;
    }
    int status;
    if (wait4(child, &status, 0, &measure->ru) == -1)
        return -1;
    /* XXX wait for iprog */
    gettimeofday(&after, NULL);
    timersub(&after, &before, &measure->real);
    measure->exit_status = WEXITSTATUS(status);
    measure->bad = measure->exit_status == 0 ? 0 : 1;
    return measure->exit_status;
}

struct measure_t *mdup(const struct measure_t *orig, unsigned count) {
    struct measure_t *copy = malloc(count*sizeof(struct measure_t));
    memcpy(copy, orig, count*sizeof(struct measure_t));
    return copy;
}

double timeval2double(const struct timeval pair)
{
    return pair.tv_sec + pair.tv_usec / (1000*1000.0);
}

long long ru_time2ll(const struct measure_t ru, size_t offset) {
    struct timeval *pair = (struct timeval *)((char *)&ru + offset);
    return pair->tv_sec * 1000 * 1000 + pair->tv_usec;
}

long long ru_long2ll(const struct measure_t ru, size_t offset) {
    long *x = (long *)((char *)&ru + offset);
    return *x;
}

struct statistic_t {
    long long min, max;
    double sd, mean;
};

void calculate_statistic(struct statistic_t *stat,
        const struct measure_t *measures, unsigned count,
        size_t offset, val_to_long *to_long) {
    long long sum = 0;
    double sd = 0.0;
    stat->max = stat->min = to_long(measures[0], offset);
    unsigned i;
    for (i = 0; i < count; ++i) {
        if (measures[i].bad)
            continue;
        long long current = to_long(measures[i], offset);
        if (current < stat->min) stat->min = current;
        if (current > stat->max) stat->max = current;
        sum += current;
    }

    stat->mean = sum/count;
    for (i = 0; i < count; ++i) {
        if (measures[i].bad)
            continue;
        long long current = to_long(measures[i], offset);
        sd += (current - stat->mean) * (current - stat->mean);
    }
    if (count != 1) {
        stat->sd = sqrt(sd/(count - 1));
    } else {
        stat->sd = 0.0;
    }
    
}

int mark_bad(const struct statistic_t *stat,
        struct measure_t *measures, unsigned count,
        size_t offset, val_to_long *to_long) {
    double high = stat->mean + 3*stat->sd;
    double low = stat->mean - 3*stat->sd;
    unsigned i, bad = 0;
    for (i = 0; i < count; ++i) {
        if (measures[i].bad)
            continue;
        long long current = to_long(measures[i], offset);
        if (low > current || high < current) {
            bad += 1;
            measures[i].bad = 1;
        }
    }
    return bad;
}

unsigned mark_bad_all(struct measure_t *measures, unsigned count) {
    #define HCALC(stat, x) calculate_statistic(&stat, measures, count, \
        offsetof(struct measure_t, x), ru_time2ll);
    #define HMARK(stat, x) mark_bad(&stat, measures, count, \
        offsetof(struct measure_t, x), ru_time2ll);
    unsigned bad = 0, i;
    struct statistic_t sr;
    struct statistic_t ss;
    struct statistic_t su;
    HCALC(sr, real);
    HCALC(ss, ru.ru_stime);
    HCALC(su, ru.ru_utime);

    HMARK(sr, real);
    HMARK(ss, ru.ru_stime);
    HMARK(su, ru.ru_utime);
    #undef HCALC
    #undef HMARK

    for (i = 0; i < count; ++i) {
        if (measures[i].bad)
            bad += 1;
    }
    return bad;
}

void dump_rusage(struct rusage ru) {
    printf("sys time     : %5.2f\n", timeval2double(ru.ru_stime));
    printf("user time    : %5.2f\n", timeval2double(ru.ru_utime));

    printf("page reclaims      : %ld\n", ru.ru_minflt);
    printf("page faults        : %ld\n", ru.ru_majflt);
    printf("vol. context switch: %ld\n", ru.ru_nvcsw);
    printf("invol. -//-  switch: %ld\n", ru.ru_nivcsw);
}

unsigned get_failed(const struct measure_t *measures, unsigned count) {
    unsigned failed=0, i;
    for (i = 0; i < count; ++i) {
        if (measures[i].exit_status != 0)
            failed += 1;
    }
    return failed;
}

void batch_print(FILE *out, const char *header, int full,
        const struct measure_t *measures, unsigned count, unsigned bad) {
    unsigned failed = get_failed(measures, count);
    fprintf(out, "# %s\n", header);
    fprintf(out, "bench_failed=%u\n", failed);
    fprintf(out, "bench_filtered=%u\n\n", bad - failed);
    #define T (1000*1000.0)
    #define TD(prefix, x) do {\
        struct statistic_t s;\
        calculate_statistic(&s, measures, count,\
            offsetof(struct measure_t, x), ru_time2ll);\
        fprintf(out, "bench_%s_min=%.3f\n", prefix, s.min/T);\
        fprintf(out, "bench_%s_mean=%.3f\n", prefix, s.mean/T);\
        fprintf(out, "bench_%s_sd=%.3f\n", prefix, s.sd/T);\
        fprintf(out, "bench_%s_max=%.3f\n", prefix, s.max/T);\
        fprintf(out, "bench_%s_min_ms=%lld\n", prefix, s.min/1000);\
        fprintf(out, "bench_%s_mean_ms=%.0f\n", prefix, s.mean/1000);\
        fprintf(out, "bench_%s_sd_ms=%.0f\n", prefix, s.sd/1000);\
        fprintf(out, "bench_%s_max_ms=%lld\n\n", prefix, s.max/1000);\
        } while (0)
    TD("real", real);
    TD("sys", ru.ru_stime);
    TD("user", ru.ru_utime);
    #undef TD
    #undef T

    #define SF(prefix, x) do {\
        struct statistic_t s;\
        calculate_statistic(&s, measures, count,\
            offsetof(struct measure_t, ru.x), ru_long2ll);\
        fprintf(out, "bench_%s_min=%lld\n", prefix, s.min);\
        fprintf(out, "bench_%s_mean=%.2f\n", prefix, s.mean);\
        fprintf(out, "bench_%s_sd=%.2f\n", prefix, s.sd);\
        fprintf(out, "bench_%s_max=%lld\n\n", prefix, s.max);\
        } while (0)
    if (full) {
        SF("minflt", ru_minflt);
        SF("majflt", ru_majflt);
        SF("nvcsw", ru_nvcsw);
        SF("nivcsw", ru_nivcsw);
    }
    #undef SF
}

void series_print(FILE *out, const char *header, int full,
        const struct measure_t *measures, unsigned count, unsigned bad) {
    unsigned i;
    (void)full;
    unsigned in_series = count/series;
    if (in_series == 0)
        in_series = 1;

    fprintf(out, "%40s\n", header);
    if (bad > 0) {
        fprintf(out, "failed run: %u\n", bad);
    }
    /* XXX I don't like to produce table in that way */
    fputs(
        "--+-----------------------+-----------------------+-----------------------\n"
        "  |       real time       |       user time       |      system time      \n"
        "  |  min    mean     max  |  min    mean     max  |  min    mean     max  \n"
        "--+-----------------------+-----------------------+-----------------------\n",
        out);
    #define T (1000*1000.0)
    #define TD(from, cnt, x) do {\
        struct statistic_t s;\
        calculate_statistic(&s, from, cnt,\
            offsetof(struct measure_t, x), ru_time2ll);\
        fprintf(out, "%6.3f [%6.3f] %6.3f ",\
            s.min/T, s.mean/T, s.max/T); \
        } while (0)
    for (i = 0; i < count; i += in_series) {
        fprintf(out, "%2u|", i/in_series + 1);
        TD(measures + i, in_series, real);
        fputs("|", out);
        TD(measures + i, in_series, ru.ru_utime);
        fputs("|", out);
        TD(measures + i, in_series, ru.ru_stime);
        fputs("\n", out);
    }
    if (in_series < count) {
        struct measure_t *copy = mdup(measures, count);
        unsigned filtered;
        filtered = mark_bad_all(copy, count);
        fputs(
            "--+-----------------------+-----------------------+-----------------------\n",
            out);
        fputs("  |", out);
        TD(copy, count, real);
        fputs("|", out);
        TD(copy, count, ru.ru_utime);
        fputs("|", out);
        TD(copy, count, ru.ru_stime);
        fputs("\n", out);
        if (filtered > 0) {
            fprintf(out, "  |   filtered out: %2u    |                       |\n", filtered);
        }
        free(copy);
    }
    #undef TD
    #undef T
    fputs(
        "--+-----------------------+-----------------------+-----------------------\n",
        out);
}

void easy_mode(FILE *out, const char *header, int full,
        const struct measure_t *measures, unsigned count, unsigned bad) {
    unsigned i;
    unsigned filtered;
    struct measure_t *copy = mdup(measures, count);
    (void)bad;
    fprintf(out, "%20s\n", header);
    fputs("-----+--------+--------+--------+----\n", out);
    fputs("     |  real  |  user  |  sys   | ec \n", out);
    fputs("-----+--------+--------+--------+----\n", out);
    for (i = 0; i < count; ++i) {
        fprintf(out, " %3u | %6.3f | %6.3f | %6.3f | %2d\n",
            i + 1,
            timeval2double(measures[i].real),
            timeval2double(measures[i].ru.ru_utime),
            timeval2double(measures[i].ru.ru_stime),
            measures[i].exit_status
        );
    }
    fputs("-----+--------+--------+--------+----\n", out);
    filtered = mark_bad_all(copy, count);
    normal_print(out, 0, full, copy, count, filtered);
    free(copy);
}

void normal_print(FILE *out, const char *header, int full,
        const struct measure_t *measures, unsigned count, unsigned bad) {
    unsigned failed = get_failed(measures, count);
    if (header)
        fprintf(out, "       %s\n", header);
    if (failed != 0) {
        fprintf(out, "program failed: %u\n", failed);
    }
    if (bad != 0) {
        fprintf(out, "filtered out: %u\n",   bad - failed);
    }
    #define T (1000*1000.0)
    #define TD(prefix, x) do {\
        struct statistic_t s;\
        calculate_statistic(&s, measures, count,\
            offsetof(struct measure_t, x), ru_time2ll);\
        fprintf(out, "%s: %6.3f [%6.3f +-%4.3f] %6.3f\n",\
            prefix, s.min/T, s.mean/T, s.sd/T, s.max/T); \
        } while (0)
    TD("real time", real);
    TD("sys  time", ru.ru_stime);
    TD("user time", ru.ru_utime);
    #undef TD
    #undef T

    #define SF(prefix, x) do {\
        struct statistic_t s;\
        calculate_statistic(&s, measures, count,\
            offsetof(struct measure_t, ru.x), ru_long2ll);\
        fprintf(out, "%s: %4lld [%5.2f +-%2.2f] %5lld\n",\
            prefix, s.min, s.mean, s.sd, s.max); \
        } while (0)
    if (full) {
        SF("page reclaims      ", ru_minflt);
        SF("page faults        ", ru_majflt);
        SF("vol. context switch", ru_nvcsw);
        SF("invol. -//-  switch", ru_nivcsw);
    }
    #undef SF
}

/**
 *  WARNING! Routine call exit(3) on errors.
 */
void calibrate(const struct output_t output, const struct slave_t slave) {
    struct measure_t measure;
    int ret;
    ret = run_program(slave, &measure);
    if (ret != 0) {
        fprintf(stderr, 
            "can't run program: exit code %d\n", ret);
        exit(1);
    }
    if (output.print_calibrate) {
        output.print_routine(output.out, "CALIBRATING", output.print_full, &measure, 1, 0);
        fprintf(output.out, "\n\n");
    }
}

int main(int argc, char **argv) {
    struct measure_t *measures;
    struct output_t output;
    struct slave_t for_test;
    unsigned i;

    DEVNULL = open(_PATH_DEVNULL, O_RDWR);
    if (DEVNULL == -1) {
        fprintf(stderr, "can't open %s: %s\n",
                _PATH_DEVNULL, strerror(errno));
    }

    if (parse_args(argc, argv, &output, &for_test) != 0) {
        return 1;
    }
    measures = calloc(output.count, sizeof(struct measure_t));

    if (output.do_calibrate) {
        calibrate(output, for_test);
    }

    for (i = 0; i < output.count; ++i) {
        run_program(for_test, measures + i);
    }

    {
        unsigned bad = 0;
        if (output.filter) {
            bad = mark_bad_all(measures, output.count);
        }
        output.print_routine(output.out, "SUMMARY", output.print_full, measures, output.count, bad);
    }
    free(for_test.args);
    free(for_test.ifile);
    free(for_test.iprog);
    free(measures);
    return 0;
}
