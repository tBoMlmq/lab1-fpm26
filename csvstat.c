#ifndef ERROR_H
#define ERROR_H

void cs_error(const char *fmt, ...);

#endif

#include <stdarg.h>
#include <stdio.h>

void cs_error(const char *fmt, ...)
{
    va_list ap;

    fputs("csvstat: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

#ifndef TYPES_H
#define TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TYPE_NULL = 0,
    TYPE_INT,
    TYPE_DOUBLE,
    TYPE_STRING
} ColType;

const char *type_name(ColType t);
ColType type_widen(ColType a, ColType b);

ColType type_classify(const char *value);

bool value_parse_int(const char *s, int64_t *out);

bool value_parse_double(const char *s, double *out);

#endif

#include <errno.h>
#include <math.h>
#include <stdlib.h>

_Static_assert(sizeof(long long) == sizeof(int64_t), "long long must be 64-bit");

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static const char *skip_sign(const char *s)
{
    return (*s == '+' || *s == '-') ? s + 1 : s;
}

static const char *skip_digits(const char *s)
{
    while (is_digit(*s))
        s++;
    return s;
}

static bool has_int_syntax(const char *s)
{
    const char *start = skip_sign(s);
    const char *end = skip_digits(start);

    return end != start && *end == '\0';
}

static bool has_fraction_syntax(const char *s)
{
    const char *int_start = skip_sign(s);
    const char *int_end = skip_digits(int_start);
    const char *frac_start;
    const char *frac_end;

    if (int_end == int_start || *int_end != '.')
        return false;
    frac_start = int_end + 1;
    frac_end = skip_digits(frac_start);
    return frac_end != frac_start && *frac_end == '\0';
}

const char *type_name(ColType t)
{
    switch (t) {
    case TYPE_NULL:   return "NULL";
    case TYPE_INT:    return "INT";
    case TYPE_DOUBLE: return "DOUBLE";
    case TYPE_STRING: return "STRING";
    }
    return "?";
}

ColType type_widen(ColType a, ColType b)
{
    return a > b ? a : b;
}

bool value_parse_int(const char *s, int64_t *out)
{
    long long v;

    if (!has_int_syntax(s))
        return false;
    errno = 0;
    v = strtoll(s, NULL, 10);
    if (errno == ERANGE)
        return false;
    *out = (int64_t)v;
    return true;
}

bool value_parse_double(const char *s, double *out)
{
    double v;

    if (!has_int_syntax(s) && !has_fraction_syntax(s))
        return false;
    v = strtod(s, NULL);
    if (!isfinite(v))
        return false;
    *out = v;
    return true;
}

ColType type_classify(const char *value)
{
    int64_t i;
    double d;

    if (value_parse_int(value, &i))
        return TYPE_INT;
    if (has_fraction_syntax(value) && value_parse_double(value, &d))
        return TYPE_DOUBLE;
    return TYPE_STRING;
}

#ifndef CSV_H
#define CSV_H

#include <stddef.h>

typedef struct CsvReader CsvReader;

CsvReader *csv_open(const char *path);
void csv_close(CsvReader *r);

int csv_next(CsvReader *r, char ***fields, size_t *nfields);

unsigned long long csv_line_no(const CsvReader *r);

#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_LINE_CAP 256
#define INITIAL_FIELD_CAP 16

struct CsvReader {
    FILE *file;
    char *line;
    size_t line_cap;
    char **fields;
    size_t field_cap;
    unsigned long long line_no;
};

static int grow_line(CsvReader *r)
{
    char *p;

    if (r->line_cap > SIZE_MAX / 2)
        return -1;
    p = realloc(r->line, r->line_cap * 2);
    if (p == NULL)
        return -1;
    r->line = p;
    r->line_cap *= 2;
    return 0;
}

static int grow_fields(CsvReader *r)
{
    char **p;

    if (r->field_cap > SIZE_MAX / 2 / sizeof(char *))
        return -1;
    p = realloc(r->fields, r->field_cap * 2 * sizeof(char *));
    if (p == NULL)
        return -1;
    r->fields = p;
    r->field_cap *= 2;
    return 0;
}

CsvReader *csv_open(const char *path)
{
    CsvReader *r = calloc(1, sizeof *r);

    if (r == NULL) {
        cs_error("out of memory");
        return NULL;
    }
    r->file = fopen(path, "rb");
    if (r->file == NULL) {
        cs_error("cannot open '%s': %s", path, strerror(errno));
        csv_close(r);
        return NULL;
    }
    r->line_cap = INITIAL_LINE_CAP;
    r->line = malloc(r->line_cap);
    r->field_cap = INITIAL_FIELD_CAP;
    r->fields = malloc(r->field_cap * sizeof(char *));
    if (r->line == NULL || r->fields == NULL) {
        cs_error("out of memory");
        csv_close(r);
        return NULL;
    }
    return r;
}

void csv_close(CsvReader *r)
{
    if (r == NULL)
        return;
    if (r->file != NULL)
        fclose(r->file);
    free(r->line);
    free(r->fields);
    free(r);
}

static int read_line(CsvReader *r, size_t *len_out)
{
    size_t len = 0;
    int c;

    for (;;) {
        c = getc(r->file);
        if (c == EOF) {
            if (ferror(r->file)) {
                cs_error("read error: %s", strerror(errno));
                return -1;
            }
            if (len == 0)
                return 0;
            break;
        }
        if (c == '\n')
            break;
        if (len + 2 > r->line_cap && grow_line(r) != 0) {
            cs_error("out of memory");
            return -1;
        }
        r->line[len++] = (char)c;
    }
    if (len + 1 > r->line_cap && grow_line(r) != 0) {
        cs_error("out of memory");
        return -1;
    }
    r->line[len] = '\0';
    *len_out = len;
    return 1;
}

int csv_next(CsvReader *r, char ***fields, size_t *nfields)
{
    size_t len = 0;
    size_t n = 1;
    size_t i;
    int rc = read_line(r, &len);

    if (rc <= 0)
        return rc;
    r->line_no++;

    r->fields[0] = r->line;
    for (i = 0; i < len; i++) {
        if (r->line[i] != ',')
            continue;
        r->line[i] = '\0';
        if (n == r->field_cap && grow_fields(r) != 0) {
            cs_error("out of memory");
            return -1;
        }
        r->fields[n++] = r->line + i + 1;
    }
    *fields = r->fields;
    *nfields = n;
    return 1;
}

unsigned long long csv_line_no(const CsvReader *r)
{
    return r->line_no;
}

#ifndef SCHEMA_H
#define SCHEMA_H

#include <stddef.h>
#include <stdio.h>

typedef struct {
    size_t ncols;
    char **names;
    ColType *types;
    unsigned long long nrows;
} Schema;

int schema_infer(const char *path, Schema *out);
void schema_free(Schema *s);

void schema_print(FILE *out, const Schema *s);

void print_quoted(FILE *out, const char *s);

#endif

#include <stdlib.h>
#include <string.h>

static char *copy_string(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);

    if (p != NULL)
        memcpy(p, s, n);
    return p;
}

void schema_free(Schema *s)
{
    size_t i;

    if (s->names != NULL) {
        for (i = 0; i < s->ncols; i++)
            free(s->names[i]);
    }
    free(s->names);
    free(s->types);
    s->names = NULL;
    s->types = NULL;
    s->ncols = 0;
}

static int read_header(CsvReader *r, Schema *s)
{
    char **fields;
    size_t n, i;
    int rc = csv_next(r, &fields, &n);

    if (rc == 0)
        cs_error("empty file");
    if (rc <= 0)
        return -1;

    s->names = calloc(n, sizeof(char *));
    s->types = calloc(n, sizeof(ColType));
    if (s->names == NULL || s->types == NULL) {
        cs_error("out of memory");
        return -1;
    }
    s->ncols = n;
    for (i = 0; i < n; i++) {
        s->names[i] = copy_string(fields[i]);
        if (s->names[i] == NULL) {
            cs_error("out of memory");
            return -1;
        }
    }
    return 0;
}

int schema_infer(const char *path, Schema *out)
{
    CsvReader *r = csv_open(path);
    char **fields;
    size_t n, i;
    int rc;

    memset(out, 0, sizeof *out);
    if (r == NULL)
        return -1;
    if (read_header(r, out) != 0)
        goto fail;

    while ((rc = csv_next(r, &fields, &n)) > 0) {
        if (n != out->ncols) {
            cs_error("line %llu: expected %zu fields, found %zu",
                     csv_line_no(r), out->ncols, n);
            goto fail;
        }
        for (i = 0; i < n; i++) {
            ColType t = type_classify(fields[i]);

            if (t == TYPE_STRING) {
                cs_error("line %llu, column %zu: value is not INT or DOUBLE",
                         csv_line_no(r), i);
                goto fail;
            }
            out->types[i] = type_widen(out->types[i], t);
        }
        out->nrows++;
    }
    if (rc < 0)
        goto fail;

    csv_close(r);
    return 0;

fail:
    csv_close(r);
    schema_free(out);
    return -1;
}

void print_quoted(FILE *out, const char *s)
{
    fputc('"', out);
    for (; *s != '\0'; s++) {
        if (*s == '"')
            fputc('"', out);
        fputc(*s, out);
    }
    fputc('"', out);
}

void schema_print(FILE *out, const Schema *s)
{
    size_t i;

    fprintf(out, "rows %llu\ncolumns %zu\n", s->nrows, s->ncols);
    for (i = 0; i < s->ncols; i++) {
        fprintf(out, "%zu ", i);
        print_quoted(out, s->names[i]);
        fprintf(out, " %s\n", type_name(s->types[i]));
    }
}

#ifndef STATS_H
#define STATS_H

#include <stdint.h>
#include <stdio.h>

typedef struct {
    int64_t hi;
    uint64_t lo;
} Sum128;

typedef struct {
    int64_t min;
    int64_t max;
    Sum128 sum;
    unsigned long long count;
    unsigned long long positive;
    unsigned long long negative;
    unsigned long long even;
} IntStats;

typedef struct {
    double min;
    double max;
    double sum;
    unsigned long long count;
    unsigned long long positive;
    unsigned long long negative;
    unsigned long long integer;
} DoubleStats;

typedef struct {
    ColType type;
    union {
        IntStats i;
        DoubleStats d;
    } u;
} ColStats;

int stats_collect(const char *path, const Schema *s, ColStats **out);

void stats_print(FILE *out, const Schema *s, const ColStats *st);

#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void sum128_add(Sum128 *s, int64_t v)
{
    uint64_t lo = s->lo + (uint64_t)v;
    int64_t carry = lo < s->lo ? 1 : 0;

    s->hi += (v < 0 ? -1 : 0) + carry;
    s->lo = lo;
}

static void sum128_abs(const Sum128 *s, uint64_t *hi, uint64_t *lo, int *negative)
{
    *negative = s->hi < 0;
    *hi = (uint64_t)s->hi;
    *lo = s->lo;
    if (*negative) {
        *lo = ~*lo + 1;
        *hi = ~*hi + (*lo == 0 ? 1 : 0);
    }
}

static void int_mean_parts(const IntStats *st, int *negative,
                           uint64_t *whole, uint64_t *frac)
{
    uint64_t hi, lo, rem = 0, quot = 0;
    uint64_t n = st->count;
    int bit;

    sum128_abs(&st->sum, &hi, &lo, negative);
    for (bit = 127; bit >= 0; bit--) {
        uint64_t in = bit >= 64 ? (hi >> (bit - 64)) & 1 : (lo >> bit) & 1;

        rem = (rem << 1) | in;
        quot <<= 1;
        if (rem >= n) {
            rem -= n;
            quot |= 1;
        }
    }
    *whole = quot;
    *frac = (rem * 1000000u + n / 2) / n;
    if (*frac >= 1000000u) {
        *frac -= 1000000u;
        (*whole)++;
    }
    if (*whole == 0 && *frac == 0)
        *negative = 0;
}

static void int_stats_add(IntStats *st, int64_t v)
{
    if (st->count == 0 || v < st->min)
        st->min = v;
    if (st->count == 0 || v > st->max)
        st->max = v;
    sum128_add(&st->sum, v);
    st->count++;
    if (v > 0)
        st->positive++;
    if (v < 0)
        st->negative++;
    if (v % 2 == 0)
        st->even++;
}

static void double_stats_add(DoubleStats *st, double v)
{
    if (st->count == 0 || v < st->min)
        st->min = v;
    if (st->count == 0 || v > st->max)
        st->max = v;
    st->sum += v;
    st->count++;
    if (v > 0)
        st->positive++;
    if (v < 0)
        st->negative++;
    if (floor(v) == v)
        st->integer++;
}

static int add_value(ColStats *st, const char *text,
                     unsigned long long line, size_t column)
{
    if (st->type == TYPE_INT) {
        int64_t v;

        if (!value_parse_int(text, &v)) {
            cs_error("line %llu, column %zu: '%s' is not an INT", line, column, text);
            return -1;
        }
        int_stats_add(&st->u.i, v);
    } else {
        double v;

        if (!value_parse_double(text, &v)) {
            cs_error("line %llu, column %zu: '%s' is not a DOUBLE", line, column, text);
            return -1;
        }
        double_stats_add(&st->u.d, v);
    }
    return 0;
}

int stats_collect(const char *path, const Schema *s, ColStats **out)
{
    ColStats *st;
    CsvReader *r;
    char **fields;
    size_t n, i;
    int rc;

    for (i = 0; i < s->ncols; i++) {
        if (s->types[i] != TYPE_INT && s->types[i] != TYPE_DOUBLE) {
            cs_error("column %zu has type %s: statistics are not supported",
                     i, type_name(s->types[i]));
            return -1;
        }
    }

    st = calloc(s->ncols, sizeof *st);
    if (st == NULL) {
        cs_error("out of memory");
        return -1;
    }
    for (i = 0; i < s->ncols; i++)
        st[i].type = s->types[i];

    r = csv_open(path);
    if (r == NULL)
        goto fail;
    if (csv_next(r, &fields, &n) <= 0)
        goto fail_close;

    while ((rc = csv_next(r, &fields, &n)) > 0) {
        if (n != s->ncols) {
            cs_error("line %llu: expected %zu fields, found %zu",
                     csv_line_no(r), s->ncols, n);
            goto fail_close;
        }
        for (i = 0; i < n; i++) {
            if (add_value(&st[i], fields[i], csv_line_no(r), i) != 0)
                goto fail_close;
        }
    }
    if (rc < 0)
        goto fail_close;

    csv_close(r);
    *out = st;
    return 0;

fail_close:
    csv_close(r);
fail:
    free(st);
    return -1;
}

static void print_real(FILE *out, long double v)
{
    if (v == 0)
        v = 0.0L;
    fprintf(out, "%.6Lf", v);
}

static void print_int_block(FILE *out, const IntStats *st)
{
    int negative;
    uint64_t whole, frac;

    int_mean_parts(st, &negative, &whole, &frac);
    fprintf(out, "nulls 0\n");
    fprintf(out, "min %lld\n", (long long)st->min);
    fprintf(out, "max %lld\n", (long long)st->max);
    fprintf(out, "mean %s%llu.%06llu\n", negative ? "-" : "",
            (unsigned long long)whole, (unsigned long long)frac);
    fprintf(out, "positive %llu\nnegative %llu\neven %llu\n",
            st->positive, st->negative, st->even);
}

static void print_double_block(FILE *out, const DoubleStats *st)
{
    double mean = st->sum / (double)st->count;

    fprintf(out, "nulls 0\n");
    fputs("min ", out);
    print_real(out, st->min);
    fputs("\nmax ", out);
    print_real(out, st->max);
    fputs("\nmean ", out);
    print_real(out, mean);
    fprintf(out, "\npositive %llu\nnegative %llu\ninteger %llu\n",
            st->positive, st->negative, st->integer);
}

void stats_print(FILE *out, const Schema *s, const ColStats *st)
{
    size_t i;

    fprintf(out, "rows %llu\ncolumns %zu\n", s->nrows, s->ncols);
    for (i = 0; i < s->ncols; i++) {
        if (i > 0)
            fputc('\n', out);
        fprintf(out, "column %zu\nname ", i);
        print_quoted(out, s->names[i]);
        fprintf(out, "\ntype %s\n", type_name(st[i].type));
        if (st[i].type == TYPE_INT)
            print_int_block(out, &st[i].u.i);
        else
            print_double_block(out, &st[i].u.d);
    }
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    EXIT_OK = 0,
    EXIT_RUNTIME = 1,
    EXIT_USAGE = 2
};

static void print_usage(FILE *out)
{
    fputs("Usage:\n"
          "  csvstat --help\n"
          "  csvstat schema <csv-file>\n"
          "  csvstat stats <csv-file>\n",
          out);
}

static int usage_error(const char *msg)
{
    cs_error("%s", msg);
    return EXIT_USAGE;
}

static int finish_stdout(void)
{
    if (fflush(stdout) != 0 || ferror(stdout)) {
        cs_error("cannot write to stdout");
        return EXIT_RUNTIME;
    }
    return EXIT_OK;
}

static int cmd_schema(const char *path)
{
    Schema s;

    if (schema_infer(path, &s) != 0)
        return EXIT_RUNTIME;
    schema_print(stdout, &s);
    schema_free(&s);
    return finish_stdout();
}

static int cmd_stats(const char *path)
{
    Schema s;
    ColStats *st = NULL;

    if (schema_infer(path, &s) != 0)
        return EXIT_RUNTIME;
    if (stats_collect(path, &s, &st) != 0) {
        schema_free(&s);
        return EXIT_RUNTIME;
    }
    stats_print(stdout, &s, st);
    free(st);
    schema_free(&s);
    return finish_stdout();
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return usage_error("missing command (try --help)");

    if (strcmp(argv[1], "--help") == 0) {
        if (argc != 2)
            return usage_error("--help takes no arguments");
        print_usage(stdout);
        return finish_stdout();
    }

    if (strcmp(argv[1], "schema") == 0 || strcmp(argv[1], "stats") == 0) {
        int is_schema = strcmp(argv[1], "schema") == 0;

        if (argc < 3)
            return usage_error("missing <csv-file>");
        if (argc > 3)
            return usage_error("unexpected extra arguments");
        return is_schema ? cmd_schema(argv[2]) : cmd_stats(argv[2]);
    }

    return usage_error("unknown command (try --help)");
}
