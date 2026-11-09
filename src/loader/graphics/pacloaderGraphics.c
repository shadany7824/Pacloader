#include "pacloaderGraphics.h"
#include "../elfLoader/glHooks.hpp"

#if defined(PACLOADER_BUILD)

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#if defined(_WIN32) || defined(__MINGW32__)
#include <windows.h>
#endif
#include <stdio.h>

#include "../config/config.h"
#include "../log/log.h"

/*
 * N2/ES1 games do not need the Lindbergh per-title shader rewriting.  Keep
 * the two Cg hooks as transparent calls because the ELF resolver uses them to
 * retain the address of the function supplied by the game's Cg runtime.
 */
void *real_cgCreateProgram = NULL;
void *real_cgGetProgramString = NULL;

/*
 * Log what the game's Cg runtime asks for. The profile number shows which
 * shader family it targets (6146 arbvp1, 6147 arbfp1, 6148 vp30, 6149 fp30,
 * 7000 vp40, 7001 fp40); the NV profiles need NVIDIA-only assembly options that
 * Intel and AMD drivers reject. Full text goes to pacloader_cg_programs.txt.
 */
static int cgLogCount = 0;

/*
 * Every successfully translated program used to be echoed to the console and appended to a
 * file, which costs a few milliseconds each at start-up. That is only useful while debugging,
 * so it is off unless LL_CG_VERBOSE=1. Programs that fail translation are always saved in
 * full to pacloader_cg_untranslated.txt regardless.
 */
static int cgVerbose(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        const char *setting = getenv("LL_CG_VERBOSE");
        cached = (setting && setting[0] == '1') ? 1 : 0;
    }
    return cached;
}

/* LL_CG_FALLBACK=0 turns the pass-through substitution off, so a failure stops the game as before. */
static int cgFallbackEnabled(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        const char *setting = getenv("LL_CG_FALLBACK");
        cached = (setting && setting[0] == '0') ? 0 : 1;
    }
    return cached;
}

static void cgLogProgram(const char *label, int profile, const char *entry, const char *text)
{
    if (!text || !cgVerbose() || cgLogCount >= 200)
        return;
    ++cgLogCount;

    char head[161];
    size_t n = 0;
    while (text[n] && n < sizeof(head) - 1)
    {
        head[n] = (text[n] == '\n' || text[n] == '\r') ? ' ' : text[n];
        ++n;
    }
    head[n] = '\0';
    if (cgLogCount <= 40)
        log_warn("Cg %s: profile=%d entry=%s text: %s", label, profile, entry ? entry : "(none)", head);

    FILE *file = fopen("pacloader_cg_programs.txt", "ab");
    if (file)
    {
        fprintf(file, "==== %s profile=%d entry=%s\n%s\n==== end\n\n", label, profile,
                entry ? entry : "(none)", text);
        fclose(file);
    }
}

/*
 * The game ships Cg "object" programs holding NVIDIA gp4 assembly (!!NVvp4.0 /
 * !!NVfp4.0). Only NVIDIA drivers accept that header; everything else wants
 * ARB assembly. Programs that stay within the subset both languages share are
 * rewritten here: the header changes, ".F" type suffixes go, ".SAT" becomes
 * "_SAT". Anything outside the shared subset is refused with a reason rather
 * than guessed at, and the program is passed through unchanged.
 */
/*
 * The game ships Cg "object" programs holding NVIDIA gp4 assembly (!!NVvp4.0 /
 * !!NVfp4.0). Only NVIDIA drivers accept that header; everything else wants
 * ARB assembly. Programs that stay within the subset both languages share are
 * rewritten here: the header changes, ".F" type suffixes go, ".SAT" becomes
 * "_SAT". Anything outside the shared subset is refused with a reason rather
 * than guessed at, and the program is passed through unchanged.
 */
/*
 * The game ships Cg "object" programs holding NVIDIA gp4 assembly (!!NVvp4.0 /
 * !!NVfp4.0). Only NVIDIA drivers accept that header; everything else wants
 * ARB assembly. Programs that stay within the subset both languages share are
 * rewritten here: the header changes, ".F" type suffixes go, ".SAT" becomes
 * "_SAT". Anything outside the shared subset is refused with a reason rather
 * than guessed at, and the program is passed through unchanged.
 */
/*
 * The game ships Cg "object" programs holding NVIDIA gp4 assembly (!!NVvp4.0 /
 * !!NVfp4.0). Only NVIDIA drivers accept that header; everything else wants
 * ARB assembly. Programs that stay within the subset both languages share are
 * rewritten here: the header changes, ".F" type suffixes go, ".SAT" becomes
 * "_SAT". Anything outside the shared subset is refused with a reason rather
 * than guessed at, and the program is passed through unchanged.
 */
/*
 * The game ships Cg "object" programs holding NVIDIA gp4 assembly (!!NVvp4.0 /
 * !!NVfp4.0). Only NVIDIA drivers accept that header; everything else wants
 * ARB assembly. Programs that stay within the subset both languages share are
 * rewritten here: the header changes, ".F" type suffixes go, ".SAT" becomes
 * "_SAT". Anything outside the shared subset is refused with a reason rather
 * than guessed at, and the program is passed through unchanged.
 */
/*
 * The game ships Cg "object" programs holding NVIDIA gp4 assembly (!!NVvp4.0 /
 * !!NVfp4.0). Only NVIDIA drivers accept that header; everything else wants
 * ARB assembly. Programs that stay within the subset both languages share are
 * rewritten here: the header changes, ".F" type suffixes go, ".SAT" becomes
 * "_SAT". Anything outside the shared subset is refused with a reason rather
 * than guessed at, and the program is passed through unchanged.
 */
/*
 * The game ships Cg "object" programs holding NVIDIA gp4 assembly (!!NVvp4.0 /
 * !!NVfp4.0). Only NVIDIA drivers accept that header; everything else wants
 * ARB assembly. Programs that stay within the subset both languages share are
 * rewritten here: the header changes, ".F" type suffixes go, ".SAT" becomes
 * "_SAT". Anything outside the shared subset is refused with a reason rather
 * than guessed at, and the program is passed through unchanged.
 */
/*
 * The game ships Cg "object" programs holding NVIDIA gp4 assembly (!!NVvp4.0 /
 * !!NVfp4.0). Only NVIDIA drivers accept that header; everything else wants
 * ARB assembly. Programs that stay within the subset both languages share are
 * rewritten here: the header changes, ".F" type suffixes go, ".SAT" becomes
 * "_SAT". Anything outside the shared subset is refused with a reason rather
 * than guessed at, and the program is passed through unchanged.
 */
/*
 * The game ships Cg "object" programs holding NVIDIA gp4 assembly (!!NVvp4.0 /
 * !!NVfp4.0). Only NVIDIA drivers accept that header; everything else wants
 * ARB assembly. Programs that stay within the subset both languages share are
 * rewritten here: the header changes, ".F" type suffixes go, ".SAT" becomes
 * "_SAT". Anything outside the shared subset is refused with a reason rather
 * than guessed at, and the program is passed through unchanged.
 */
/*
 * The game ships Cg "object" programs holding NVIDIA gp4 assembly (!!NVvp4.0 /
 * !!NVfp4.0). Only NVIDIA drivers accept that header; everything else wants
 * ARB assembly. Programs that stay within the subset both languages share are
 * rewritten here: the header changes, ".F" type suffixes go, ".SAT" becomes
 * "_SAT". Anything outside the shared subset is refused with a reason rather
 * than guessed at, and the program is passed through unchanged.
 */
/*
 * The game ships Cg "object" programs holding NVIDIA gp4 assembly (!!NVvp4.0 /
 * !!NVfp4.0). Only NVIDIA drivers accept that header; everything else wants
 * ARB assembly. Programs that stay within the subset both languages share are
 * rewritten here: the header changes, ".F" type suffixes go, ".SAT" becomes
 * "_SAT". Anything outside the shared subset is refused with a reason rather
 * than guessed at, and the program is passed through unchanged.
 */
/*
 * The game ships Cg "object" programs holding NVIDIA gp4 assembly (!!NVvp4.0 /
 * !!NVfp4.0). Only NVIDIA drivers accept that header; everything else wants
 * ARB assembly. Programs that stay within the subset both languages share are
 * rewritten here: the header changes, ".F" type suffixes go, ".SAT" becomes
 * "_SAT". Anything outside the shared subset is refused with a reason rather
 * than guessed at, and the program is passed through unchanged.
 */
/*
 * The game ships Cg "object" programs holding NVIDIA gp4 assembly (!!NVvp4.0 /
 * !!NVfp4.0). Only NVIDIA drivers accept that header; everything else wants
 * ARB assembly. Programs that stay within the subset both languages share are
 * rewritten here: the header changes, ".F" type suffixes go, ".SAT" becomes
 * "_SAT". Anything outside the shared subset is refused with a reason rather
 * than guessed at, and the program is passed through unchanged.
 */
/*
 * The game ships Cg "object" programs holding NVIDIA gp4 assembly (!!NVvp4.0 /
 * !!NVfp4.0). Only NVIDIA drivers accept that header; everything else wants
 * ARB assembly. Programs that stay within the subset both languages share are
 * rewritten here: the header changes, ".F" type suffixes go, ".SAT" becomes
 * "_SAT". Anything outside the shared subset is refused with a reason rather
 * than guessed at, and the program is passed through unchanged.
 */
typedef struct
{
    char *data;
    size_t length;
    size_t capacity;
} PacTextBuffer;

static int pacTextAppend(PacTextBuffer *buffer, const char *text, size_t count)
{
    if (buffer->length + count + 1 > buffer->capacity)
    {
        size_t capacity = buffer->capacity ? buffer->capacity : 1024;
        while (buffer->length + count + 1 > capacity)
            capacity *= 2;
        char *grown = (char *)realloc(buffer->data, capacity);
        if (!grown)
            return 0;
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, text, count);
    buffer->length += count;
    buffer->data[buffer->length] = '\0';
    return 1;
}

static int pacWordIn(const char *word, const char *const *list)
{
    for (; *list; ++list)
    {
        if (strcmp(word, *list) == 0)
            return 1;
    }
    return 0;
}

static int pacEmitf(PacTextBuffer *out, const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    const int count = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= sizeof(text))
        return 0;
    return pacTextAppend(out, text, (size_t)count);
}

/* Splits "a, b, c;" at top-level commas (not inside {} or []). Edits text in place. */
static int pacSplitOperands(char *text, char **parts, int maxParts)
{
    char *p = text;
    while (*p == ' ' || *p == '\t')
        ++p;
    size_t length = strlen(p);
    while (length > 0 && (p[length - 1] == ';' || p[length - 1] == ' ' || p[length - 1] == '\t'))
        p[--length] = '\0';
    if (length == 0)
        return 0;

    int count = 0;
    int depth = 0;
    char *start = p;
    for (;; ++p)
    {
        if (*p == '{' || *p == '[')
            ++depth;
        else if (*p == '}' || *p == ']')
            --depth;
        if ((*p == ',' && depth == 0) || *p == '\0')
        {
            const int last = (*p == '\0');
            *p = '\0';
            while (*start == ' ' || *start == '\t')
                ++start;
            char *end = p;
            while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
                *--end = '\0';
            if (count >= maxParts)
                return -1;
            parts[count++] = start;
            if (last)
                break;
            start = p + 1;
        }
    }
    return count;
}

typedef struct
{
    char sign[2];
    char base[96];
    char swizzle[5];
} PacOperand;

/* "-c[4].xy" -> sign "-", base "c[4]", swizzle "xy". */
static int pacParseOperand(const char *text, PacOperand *operand)
{
    operand->sign[0] = '\0';
    operand->swizzle[0] = '\0';
    if (*text == '-')
    {
        operand->sign[0] = '-';
        operand->sign[1] = '\0';
        ++text;
    }
    else if (*text == '+')
        ++text;

    size_t baseLength = strlen(text);
    const char *dot = strrchr(text, '.');
    if (dot)
    {
        const size_t tail = strlen(dot + 1);
        if (tail >= 1 && tail <= 4 && strspn(dot + 1, "xyzwrgba") == tail)
        {
            memcpy(operand->swizzle, dot + 1, tail);
            operand->swizzle[tail] = '\0';
            baseLength = (size_t)(dot - text);
        }
    }
    if (baseLength >= sizeof(operand->base))
        return 0;
    memcpy(operand->base, text, baseLength);
    operand->base[baseLength] = '\0';
    return 1;
}

static void pacNegate(const char *text, char *out, size_t outSize)
{
    if (text[0] == '-')
        snprintf(out, outSize, "%s", text + 1);
    else
        snprintf(out, outSize, "-%s", text);
}

/*
 * gp4 allows |operand| (absolute value) on any source; ARB does not. Each |x| is
 * run through ABS into a helper temp emitted just before the instruction, and the
 * temp replaces it. Rewrites "work" in place. Returns 0 (reason set) on failure.
 */
static int pacExtractAbs(PacTextBuffer *out, char *work, size_t workSize, int *tempFlags,
                         char *reason, size_t reasonSize)
{
    if (!strchr(work, '|'))
        return 1;

    char rewritten[512];
    size_t used = 0;
    int count = 0;
    const char *cursor = work;
    while (*cursor)
    {
        if (*cursor == '|')
        {
            const char *close = strchr(cursor + 1, '|');
            if (!close || count >= 4)
            {
                snprintf(reason, reasonSize, "unbalanced or too many |abs| operands");
                return 0;
            }
            const size_t innerLength = (size_t)(close - cursor - 1);
            char inner[128];
            if (innerLength == 0 || innerLength >= sizeof(inner))
            {
                snprintf(reason, reasonSize, "unsupported |abs| operand");
                return 0;
            }
            memcpy(inner, cursor + 1, innerLength);
            inner[innerLength] = '\0';
            if (!pacEmitf(out, "ABS pacA%d, %s;\n", count, inner))
            {
                snprintf(reason, reasonSize, "could not expand |abs| operand");
                return 0;
            }
            const int added = snprintf(rewritten + used, sizeof(rewritten) - used, "pacA%d", count);
            if (added < 0 || (size_t)added >= sizeof(rewritten) - used)
            {
                snprintf(reason, reasonSize, "operands too long");
                return 0;
            }
            used += (size_t)added;
            ++count;
            cursor = close + 1;
        }
        else
        {
            if (used + 1 >= sizeof(rewritten))
            {
                snprintf(reason, reasonSize, "operands too long");
                return 0;
            }
            rewritten[used++] = *cursor++;
        }
    }
    rewritten[used] = '\0';
    if (used >= workSize)
    {
        snprintf(reason, reasonSize, "operands too long");
        return 0;
    }
    memcpy(work, rewritten, used + 1);
    *tempFlags |= 4;
    return 1;
}

/*
 * Integer values. ARB has no integer registers. TRUNC.U/.S and I2F are handled by keeping the
 * integer in a float that holds the same value, which is exact as long as every reader of that
 * register component is I2F. integerMarks[n] records which components of Rn hold an integer;
 * anything else reading one is refused rather than guessed at.
 */
static unsigned pacLetterBit(char letter)
{
    switch (letter)
    {
        case 'x': case 'r': return 1;
        case 'y': case 'g': return 2;
        case 'z': case 'b': return 4;
        case 'w': case 'a': return 8;
        default: return 0;
    }
}

static int pacRegisterIndex(const char *base)
{
    if (base[0] != 'R' || !isdigit((unsigned char)base[1]))
        return -1;
    int value = 0;
    for (const char *c = base + 1; *c; ++c)
    {
        if (!isdigit((unsigned char)*c))
            return -1;
        value = value * 10 + (*c - '0');
    }
    return value < 16 ? value : -1;
}

/* The letters a source supplies at the given positions (bit p = position p of the swizzled source). */
static unsigned pacLettersAt(const PacOperand *source, unsigned positions)
{
    static const char xyzw[] = "xyzw";
    const size_t length = strlen(source->swizzle);
    unsigned used = 0;
    for (size_t p = 0; p < 4; ++p)
    {
        if (!(positions & (1u << p)))
            continue;
        char letter;
        if (length == 0)
            letter = xyzw[p];
        else if (length == 1)
            letter = source->swizzle[0];
        else
            letter = source->swizzle[p < length ? p : 0];
        used |= pacLetterBit(letter);
    }
    return used;
}

/* Positions 0-3 touched by writing the components named in a write mask. */
static unsigned pacMaskPositions(const char *mask)
{
    static const char xyzw[] = "xyzw";
    unsigned positions = 0;
    for (const char *c = mask; *c; ++c)
    {
        const char *position = strchr(xyzw, *c);
        if (position)
            positions |= 1u << (position - xyzw);
    }
    return positions;
}

/*
 * Which swizzle positions of source operand number "operandIndex" (1 = first source) an
 * instruction reads. Most instructions work per component, so the write mask decides; dot
 * products and texture lookups read a fixed number of components, and a lookup only needs as
 * many coordinates as its target has dimensions.
 */
static unsigned pacOperandPositions(const char *opcode, int operandIndex, const char *target,
                                    const char *destinationMask)
{
    if (strcmp(opcode, "DP2") == 0)
        return 3;
    if (strcmp(opcode, "DP3") == 0)
        return 7;
    if (strcmp(opcode, "DP4") == 0)
        return 15;
    if (strcmp(opcode, "DPH") == 0)
        return operandIndex == 1 ? 7 : 15;
    if (strcmp(opcode, "TEX") == 0 || strcmp(opcode, "TXB") == 0 || strcmp(opcode, "TXP") == 0)
    {
        if (operandIndex != 1)
            return 15;
        unsigned count = 4; /* unknown target: assume everything */
        if (strcmp(target, "1D") == 0)
            count = 1;
        else if (strcmp(target, "2D") == 0 || strcmp(target, "RECT") == 0)
            count = 2;
        else if (strcmp(target, "3D") == 0 || strcmp(target, "CUBE") == 0 ||
                 strncmp(target, "SHADOW", 6) == 0)
            count = 3;
        unsigned positions = (1u << count) - 1u;
        if (opcode[2] != 'X') /* TXB/TXP also read w (bias / projective divide) */
            positions |= 8u;
        return positions;
    }
    static const char *const wholeVector[] = {"KIL", "XPD", "DST", "LIT", "SCS", "SIN", "COS", NULL};
    if (pacWordIn(opcode, wholeVector))
        return 15;
    return pacMaskPositions(destinationMask);
}

/* Returns 1 (and names the register) if the instruction reads an integer-holding component. */
static int pacReadsInteger(const char *operandText, const unsigned char *marks, const char *opcode,
                           char *which, size_t whichSize)
{
    char text[512];
    if (strlen(operandText) >= sizeof(text))
        return 0;
    size_t length = 0;
    for (const char *c = operandText; *c; ++c)
        if (*c != '|')
            text[length++] = *c;
    text[length] = '\0';

    char *parts[8];
    const int count = pacSplitOperands(text, parts, 8);
    PacOperand destination;
    if (count < 2 || !pacParseOperand(parts[0], &destination))
        return 0;
    const char *mask = destination.swizzle[0] ? destination.swizzle : "xyzw";
    const char *target = parts[count - 1];
    for (int i = 1; i < count; ++i)
    {
        PacOperand source;
        if (!pacParseOperand(parts[i], &source))
            continue;
        const int index = pacRegisterIndex(source.base);
        if (index < 0 || !marks[index])
            continue;
        if (pacLettersAt(&source, pacOperandPositions(opcode, i, target, mask)) & marks[index])
        {
            snprintf(which, whichSize, "%.40s", source.base);
            return 1;
        }
    }
    return 0;
}

/*
 * gp4 instructions that ARB lacks but that reduce exactly to ARB ones.
 * Returns 1 when emitted, -1 on failure (reason set).
 */
static int pacEmitExpanded(PacTextBuffer *out, const char *opcode, int saturate,
                           char *operandText, int *tempsDeclared, const char *guardMask,
                           char *reason, size_t reasonSize)
{
    const char *sat = saturate ? "_SAT" : "";
    char *p[4];
    const int count = pacSplitOperands(operandText, p, 4);
    const int wanted = (strcmp(opcode, "CEIL") == 0 || strcmp(opcode, "TRUNC") == 0 ||
                        strcmp(opcode, "TRUNCU") == 0 || strcmp(opcode, "I2F") == 0)
                           ? 2
                           : 3;
    if (count != wanted)
    {
        snprintf(reason, reasonSize, "%s with %d operands", opcode, count);
        return -1;
    }
    *tempsDeclared |= 1; /* declared once, at the top of the program, afterwards */

    int ok = 1;
    if (strcmp(opcode, "DIV") == 0)
    {
        PacOperand divisor;
        if (!pacParseOperand(p[2], &divisor))
        {
            snprintf(reason, reasonSize, "DIV operand too long");
            return -1;
        }
        const size_t swizzleLength = strlen(divisor.swizzle);
        if (guardMask && !guardMask[0])
        {
            snprintf(reason, reasonSize, "DIV under a multi-condition predicate");
            return -1;
        }
        if (swizzleLength == 1)
        {
            ok = (!guardMask || (pacEmitf(out, "MOV pacT1.x, %s%s.%s;\n", divisor.sign, divisor.base,
                                          divisor.swizzle) &&
                                 pacEmitf(out, "SUB pacT1.x, pacT1.x, {1}.x;\n") &&
                                 pacEmitf(out, "MAD pacT1.x, %s, pacT1.x, {1}.x;\n", guardMask))) &&
                 (guardMask ? pacEmitf(out, "RCP pacT0.x, pacT1.x;\n")
                            : pacEmitf(out, "RCP pacT0.x, %s%s.%s;\n", divisor.sign, divisor.base,
                                       divisor.swizzle)) &&
                 pacEmitf(out, "MUL%s %s, %s, pacT0.x;\n", sat, p[0], p[1]);
        }
        else if (swizzleLength == 0 || swizzleLength == 4)
        {
            static const char components[] = "xyzw";
            const char *letters = swizzleLength == 4 ? divisor.swizzle : components;
            for (int i = 0; ok && i < 4; ++i)
            {
                if (guardMask)
                    ok = pacEmitf(out, "MOV pacT1.x, %s%s.%c;\n", divisor.sign, divisor.base, letters[i]) &&
                         pacEmitf(out, "SUB pacT1.x, pacT1.x, {1}.x;\n") &&
                         pacEmitf(out, "MAD pacT1.x, %s, pacT1.x, {1}.x;\n", guardMask) &&
                         pacEmitf(out, "RCP pacT0.%c, pacT1.x;\n", components[i]);
                else
                    ok = pacEmitf(out, "RCP pacT0.%c, %s%s.%c;\n", components[i], divisor.sign,
                                  divisor.base, letters[i]);
            }
            ok = ok && pacEmitf(out, "MUL%s %s, %s, pacT0;\n", sat, p[0], p[1]);
        }
        else
        {
            snprintf(reason, reasonSize, "DIV with a %zu-component divisor swizzle", swizzleLength);
            return -1;
        }
    }
    else if (strcmp(opcode, "SGT") == 0)
        ok = pacEmitf(out, "SLT%s %s, %s, %s;\n", sat, p[0], p[2], p[1]);
    else if (strcmp(opcode, "SLE") == 0)
        ok = pacEmitf(out, "SGE%s %s, %s, %s;\n", sat, p[0], p[2], p[1]);
    else if (strcmp(opcode, "SEQ") == 0)
        ok = pacEmitf(out, "SGE pacT0, %s, %s;\n", p[1], p[2]) &&
             pacEmitf(out, "SGE pacT1, %s, %s;\n", p[2], p[1]) &&
             pacEmitf(out, "MUL%s %s, pacT0, pacT1;\n", sat, p[0]);
    else if (strcmp(opcode, "SNE") == 0)
        ok = pacEmitf(out, "SLT pacT0, %s, %s;\n", p[1], p[2]) &&
             pacEmitf(out, "SLT pacT1, %s, %s;\n", p[2], p[1]) &&
             pacEmitf(out, "ADD%s %s, pacT0, pacT1;\n", sat, p[0]);
    else if (strcmp(opcode, "DP2") == 0)
        ok = pacEmitf(out, "MUL pacT0, %s, %s;\n", p[1], p[2]) &&
             pacEmitf(out, "ADD%s %s, pacT0.x, pacT0.y;\n", sat, p[0]);
    else if (strcmp(opcode, "TRUNC") == 0)
    {
        /* sign(a) * floor(|a|), built so no instruction reads two parameters. */
        ok = pacEmitf(out, "MOV pacT0, %s;\n", p[1]) && pacEmitf(out, "ABS pacT1, pacT0;\n") &&
             pacEmitf(out, "FLR pacT1, pacT1;\n") && pacEmitf(out, "SGE pacT0, pacT0, {0}.x;\n") &&
             pacEmitf(out, "ADD pacT0, pacT0, pacT0;\n") &&
             pacEmitf(out, "ADD pacT0, pacT0, -{1}.x;\n") &&
             pacEmitf(out, "MUL%s %s, pacT1, pacT0;\n", sat, p[0]);
    }
    else if (strcmp(opcode, "TRUNCU") == 0)
    {
        /* Unsigned: truncate toward zero, negatives become 0 = floor(|a|) * (a >= 0). */
        ok = pacEmitf(out, "MOV pacT0, %s;\n", p[1]) && pacEmitf(out, "ABS pacT1, pacT0;\n") &&
             pacEmitf(out, "FLR pacT1, pacT1;\n") && pacEmitf(out, "SGE pacT0, pacT0, {0}.x;\n") &&
             pacEmitf(out, "MUL%s %s, pacT1, pacT0;\n", sat, p[0]);
    }
    else if (strcmp(opcode, "I2F") == 0)
    {
        /* The integer is already held as a float of the same value. */
        ok = pacEmitf(out, "MOV%s %s, %s;\n", sat, p[0], p[1]);
    }
    else if (strcmp(opcode, "CEIL") == 0)
    {
        char negated[128];
        pacNegate(p[1], negated, sizeof(negated));
        ok = pacEmitf(out, "FLR pacT0, %s;\n", negated) &&
             pacEmitf(out, "MOV%s %s, -pacT0;\n", sat, p[0]);
    }
    if (!ok)
    {
        snprintf(reason, reasonSize, "could not expand %s", opcode);
        return -1;
    }
    return 1;
}

static int pacOpcodeAllowed(const char *opcode, int fragment)
{
    static const char *const shared[] = {"ABS", "ADD", "DP3", "DP4", "DPH", "DST", "EX2", "FLR",
                                         "FRC", "LG2", "LIT", "MAD", "MAX", "MIN", "MOV", "MUL",
                                         "POW", "RCP", "RSQ", "SGE", "SLT", "SUB", "SWZ", "XPD",
                                         NULL};
    static const char *const vertexOnly[] = {"ARL", "EXP", "LOG", NULL};
    static const char *const fragmentOnly[] = {"CMP", "COS", "KIL", "LRP", "SCS", "SIN",
                                               "TEX", "TXB", "TXP", NULL};
    return pacWordIn(opcode, shared) ||
           pacWordIn(opcode, fragment ? fragmentOnly : vertexOnly);
}

/* Returns a malloc'd ARB program, or NULL. *reason is empty when the text was
 * not NVIDIA assembly at all and needs no translation. */
/*
 * Counted loops. gp4 has real loops (REP ... BRK ... ENDREP); ARB does not. The NVIDIA compiler
 * writes "for (int i = 0; i < N; i++)" in one fixed shape, and when the trip count is a
 * constant and the body never touches the counter, unrolling it is exact. Only that shape is
 * accepted; the counter and its scratch register are left holding the values the loop would
 * have left behind.
 */
typedef struct
{
    char opcode[16];
    char mods[16];
    char operands[256];
} PacStatement;

static int pacParseStatement(const char *line, PacStatement *statement)
{
    while (*line == ' ' || *line == '\t')
        ++line;
    if (!*line || *line == '#')
        return 0;
    size_t n = 0;
    while (isalnum((unsigned char)line[n]) || line[n] == '_')
        ++n;
    if (n == 0 || n >= sizeof(statement->opcode))
        return 0;
    memcpy(statement->opcode, line, n);
    statement->opcode[n] = '\0';
    const char *p = line + n;
    size_t m = 0;
    while (*p == '.')
    {
        if (m + 1 >= sizeof(statement->mods))
            return 0;
        statement->mods[m++] = *p++;
        while (isalnum((unsigned char)*p) || *p == '_')
        {
            if (m + 1 >= sizeof(statement->mods))
                return 0;
            statement->mods[m++] = *p++;
        }
    }
    statement->mods[m] = '\0';
    while (*p == ' ' || *p == '\t')
        ++p;
    size_t length = strlen(p);
    while (length > 0 && (p[length - 1] == ';' || p[length - 1] == ' ' || p[length - 1] == '\t' ||
                          p[length - 1] == '\r'))
        --length;
    if (length >= sizeof(statement->operands))
        return 0;
    memcpy(statement->operands, p, length);
    statement->operands[length] = '\0';
    return 1;
}

/* Index of the next statement at or after "from", skipping blank and comment lines; -1 if none. */
static int pacNextStatement(char **lines, int count, int from, PacStatement *statement)
{
    for (int i = from; i < count; ++i)
        if (pacParseStatement(lines[i], statement))
            return i;
    return -1;
}

static int pacPreviousStatement(char **lines, int from, PacStatement *statement)
{
    for (int i = from; i >= 0; --i)
        if (pacParseStatement(lines[i], statement))
            return i;
    return -1;
}

/* Splits a statement's operands; returns the count or -1. "storage" must outlive "parts". */
static int pacStatementOperands(const PacStatement *statement, char *storage, size_t storageSize,
                                char **parts, int maxParts)
{
    if (strlen(statement->operands) >= storageSize)
        return -1;
    memcpy(storage, statement->operands, strlen(statement->operands) + 1);
    return pacSplitOperands(storage, parts, maxParts);
}

static int pacOperandIs(const char *text, const char *base, char letter, int allowNoSwizzle, int allowNegative)
{
    PacOperand operand;
    if (!pacParseOperand(text, &operand))
        return 0;
    if (operand.sign[0] && !allowNegative)
        return 0;
    if (!operand.sign[0] && allowNegative == 2)
        return 0; /* a minus sign is required */
    if (strcmp(operand.base, base) != 0)
        return 0;
    if (operand.swizzle[0] == '\0')
        return allowNoSwizzle;
    return strlen(operand.swizzle) == 1 && operand.swizzle[0] == letter;
}

/* Is this a known loop-control or flow word that a loop body must not contain? */
static int pacIsControlWord(const char *opcode)
{
    static const char *const words[] = {"REP", "ENDREP", "BRK", "CONT", "IF", "ELSE", "ENDIF", "CAL",
                                        "RET", "LOOP", "ENDLOOP", NULL};
    return pacWordIn(opcode, words);
}

#define PAC_LOOP_FAIL(...)                                                                         \
    do                                                                                             \
    {                                                                                              \
        char detail[120];                                                                          \
        snprintf(detail, sizeof(detail), __VA_ARGS__);                                             \
        snprintf(reason, reasonSize, "REP loop is not in the supported counted-loop form: %.100s", \
                 detail);                                                                          \
        status = -1;                                                                               \
        goto done;                                                                                 \
    } while (0)

/* Returns 0 (no loop), 1 (unrolled; *result set) or -1 (loop present but unsupported; reason set). */
static int pacUnrollOneLoop(const char *source, char **result, char *reason, size_t reasonSize)
{
    int status = 0;
    char *storage = NULL;
    char **lines = NULL;
    PacTextBuffer out = {NULL, 0, 0};
    *result = NULL;

    const size_t sourceLength = strlen(source);
    storage = (char *)malloc(sourceLength + 1);
    if (!storage)
        return -1;
    memcpy(storage, source, sourceLength + 1);
    int count = 1;
    for (const char *c = storage; *c; ++c)
        if (*c == '\n')
            ++count;
    lines = (char **)malloc((size_t)count * sizeof(char *));
    if (!lines)
    {
        free(storage);
        return -1;
    }
    {
        int n = 0;
        char *cursor = storage;
        for (;;)
        {
            lines[n++] = cursor;
            char *newline = strchr(cursor, '\n');
            if (!newline)
                break;
            *newline = '\0';
            cursor = newline + 1;
        }
    }

    PacStatement statement;
    int repLine = -1;
    for (int i = 0; i < count && repLine < 0; ++i)
        if (pacParseStatement(lines[i], &statement) && strcmp(statement.opcode, "REP") == 0)
            repLine = i;
    if (repLine < 0)
        goto done;

    char storageA[256];
    char *parts[8];

    /* i = 0 */
    int initLine = pacPreviousStatement(lines, repLine - 1, &statement);
    PacOperand counter, scratch;
    if (initLine < 0 || strcmp(statement.opcode, "MOV") != 0 || strcmp(statement.mods, ".S") != 0 ||
        pacStatementOperands(&statement, storageA, sizeof(storageA), parts, 8) != 2 ||
        !pacParseOperand(parts[0], &counter) || strlen(counter.swizzle) != 1 ||
        pacRegisterIndex(counter.base) < 0 || strcmp(parts[1], "{0}.x") != 0)
        PAC_LOOP_FAIL("the counter is not initialised with MOV.S Rn.c, {0}.x");
    const char counterLetter = counter.swizzle[0];

    /* prologue: I2F, SLT, TRUNC.U, SEQ.U, MOV.U.CC, BRK */
    int cursor = repLine + 1;
    int line = pacNextStatement(lines, count, cursor, &statement);
    if (line < 0 || strcmp(statement.opcode, "I2F") != 0 ||
        pacStatementOperands(&statement, storageA, sizeof(storageA), parts, 8) != 2 ||
        !pacParseOperand(parts[0], &scratch) || strlen(scratch.swizzle) != 1 ||
        pacRegisterIndex(scratch.base) < 0 || !pacOperandIs(parts[1], counter.base, counterLetter, 0, 0))
        PAC_LOOP_FAIL("expected I2F of the counter");
    const char scratchLetter = scratch.swizzle[0];

    line = pacNextStatement(lines, count, line + 1, &statement);
    double tripCount = 0;
    if (line < 0 || strcmp(statement.opcode, "SLT") != 0 || strcmp(statement.mods, ".F") != 0 ||
        pacStatementOperands(&statement, storageA, sizeof(storageA), parts, 8) != 3 ||
        !pacOperandIs(parts[0], scratch.base, scratchLetter, 0, 0) ||
        !pacOperandIs(parts[1], scratch.base, scratchLetter, 1, 0) || parts[2][0] != '{')
        PAC_LOOP_FAIL("expected SLT.F counter < constant");
    tripCount = atof(parts[2] + 1);
    if (!(tripCount >= 1 && tripCount <= 64 && tripCount == (double)(int)tripCount))
        PAC_LOOP_FAIL("trip count %g is not a whole number from 1 to 64", tripCount);
    const int trips = (int)tripCount;

    line = pacNextStatement(lines, count, line + 1, &statement);
    if (line < 0 || strcmp(statement.opcode, "TRUNC") != 0 || strcmp(statement.mods, ".U") != 0 ||
        pacStatementOperands(&statement, storageA, sizeof(storageA), parts, 8) != 2 ||
        !pacOperandIs(parts[0], scratch.base, scratchLetter, 0, 0) ||
        !pacOperandIs(parts[1], scratch.base, scratchLetter, 1, 0))
        PAC_LOOP_FAIL("expected TRUNC.U of the comparison");

    line = pacNextStatement(lines, count, line + 1, &statement);
    if (line < 0 || strcmp(statement.opcode, "SEQ") != 0 || strcmp(statement.mods, ".U") != 0 ||
        pacStatementOperands(&statement, storageA, sizeof(storageA), parts, 8) != 3 ||
        !pacOperandIs(parts[0], scratch.base, scratchLetter, 0, 0) ||
        !pacOperandIs(parts[1], scratch.base, scratchLetter, 1, 0) || strcmp(parts[2], "{0}.x") != 0)
        PAC_LOOP_FAIL("expected SEQ.U against zero");

    line = pacNextStatement(lines, count, line + 1, &statement);
    if (line < 0 || strcmp(statement.opcode, "MOV") != 0 || strcmp(statement.mods, ".U.CC") != 0 ||
        pacStatementOperands(&statement, storageA, sizeof(storageA), parts, 8) != 2 ||
        strcmp(parts[0], "RC.x") != 0 || !pacOperandIs(parts[1], scratch.base, scratchLetter, 1, 2))
        PAC_LOOP_FAIL("expected MOV.U.CC RC.x, -flag");

    line = pacNextStatement(lines, count, line + 1, &statement);
    if (line < 0 || strcmp(statement.opcode, "BRK") != 0 || strcmp(statement.operands, "(GT.x)") != 0)
        PAC_LOOP_FAIL("expected BRK (GT.x)");
    const int bodyStart = line + 1;

    /* body ... ADD.S counter, counter, {1}.x; ENDREP; */
    int endLine = -1;
    for (int i = bodyStart; i < count; ++i)
    {
        if (!pacParseStatement(lines[i], &statement))
            continue;
        if (strcmp(statement.opcode, "ENDREP") == 0)
        {
            endLine = i;
            break;
        }
        if (strcmp(statement.opcode, "REP") == 0)
            PAC_LOOP_FAIL("nested loops");
    }
    if (endLine < 0)
        PAC_LOOP_FAIL("no ENDREP");
    int incrementLine = pacPreviousStatement(lines, endLine - 1, &statement);
    if (incrementLine < bodyStart || strcmp(statement.opcode, "ADD") != 0 ||
        strcmp(statement.mods, ".S") != 0 ||
        pacStatementOperands(&statement, storageA, sizeof(storageA), parts, 8) != 3 ||
        !pacOperandIs(parts[0], counter.base, counterLetter, 0, 0) ||
        !pacOperandIs(parts[1], counter.base, counterLetter, 1, 0) || strcmp(parts[2], "{1}.x") != 0)
        PAC_LOOP_FAIL("the counter is not incremented by ADD.S Rn.c, Rn.c, {1}.x");

    /* The body must be identical on every pass: no flow control, no use of the counter, and
     * no reliance on the value the loop test left in the scratch component. */
    const unsigned counterBit = pacLetterBit(counterLetter);
    const unsigned scratchBit = pacLetterBit(scratchLetter);
    int scratchWritten = 0;
    for (int i = bodyStart; i < incrementLine; ++i)
    {
        if (!pacParseStatement(lines[i], &statement))
            continue;
        if (pacIsControlWord(statement.opcode) || strchr(lines[i], '('))
            PAC_LOOP_FAIL("flow control or a predicate inside the body");
        char bodyText[256];
        if (strlen(statement.operands) >= sizeof(bodyText))
            PAC_LOOP_FAIL("a body line is too long");
        size_t used = 0;
        for (const char *c = statement.operands; *c; ++c)
            if (*c != '|')
                bodyText[used++] = *c;
        bodyText[used] = '\0';
        char *bodyParts[8];
        const int bodyCount = pacSplitOperands(bodyText, bodyParts, 8);
        PacOperand destination;
        if (bodyCount < 2 || !pacParseOperand(bodyParts[0], &destination))
            PAC_LOOP_FAIL("an instruction in the body has no destination");
        const char *mask = destination.swizzle[0] ? destination.swizzle : "xyzw";
        const unsigned writeBits = pacMaskPositions(mask);
        for (int k = 1; k < bodyCount; ++k)
        {
            PacOperand source;
            if (!pacParseOperand(bodyParts[k], &source))
                continue;
            const unsigned used_letters =
                pacLettersAt(&source, pacOperandPositions(statement.opcode, k, bodyParts[bodyCount - 1], mask));
            if (strcmp(source.base, counter.base) == 0 && (used_letters & counterBit))
                PAC_LOOP_FAIL("the body reads the loop counter");
            if (strcmp(source.base, scratch.base) == 0 && (used_letters & scratchBit) && !scratchWritten)
                PAC_LOOP_FAIL("the body reads the loop-test scratch value");
        }
        if (strcmp(destination.base, counter.base) == 0 && (writeBits & pacLetterBit(counterLetter) ? 1 : 0))
            PAC_LOOP_FAIL("the body writes the loop counter");
        if (strcmp(destination.base, scratch.base) == 0)
        {
            unsigned lettersWritten = 0;
            for (const char *c = mask; *c; ++c)
                lettersWritten |= pacLetterBit(*c);
            if (lettersWritten & scratchBit)
                scratchWritten = 1;
        }
    }

    /* Emit: everything before the counter init, the body repeated, the final counter and
     * scratch values, then everything after ENDREP. */
    for (int i = 0; i < initLine; ++i)
        if (!pacTextAppend(&out, lines[i], strlen(lines[i])) || !pacTextAppend(&out, "\n", 1))
            PAC_LOOP_FAIL("out of memory");
    for (int pass = 0; pass < trips; ++pass)
        for (int i = bodyStart; i < incrementLine; ++i)
            if (!pacTextAppend(&out, lines[i], strlen(lines[i])) || !pacTextAppend(&out, "\n", 1))
                PAC_LOOP_FAIL("out of memory");
    if (!pacEmitf(&out, "MOV.S %s.%c, {%d}.x;\n", counter.base, counterLetter, trips) ||
        !pacEmitf(&out, "MOV.S %s.%c, {-1}.x;\n", scratch.base, scratchLetter))
        PAC_LOOP_FAIL("out of memory");
    for (int i = endLine + 1; i < count; ++i)
        if (!pacTextAppend(&out, lines[i], strlen(lines[i])) ||
            (i + 1 < count && !pacTextAppend(&out, "\n", 1)))
            PAC_LOOP_FAIL("out of memory");

    *result = out.data;
    out.data = NULL;
    status = 1;

done:
    free(out.data);
    free(lines);
    free(storage);
    return status;
}

static char *pacTranslateNvProgramCore(const char *source, char *reason, size_t reasonSize);

static char *pacTranslateNvProgram(const char *source, char *reason, size_t reasonSize)
{
    char *current = NULL;
    reason[0] = '\0';
    for (int pass = 0; pass < 4; ++pass)
    {
        char *unrolled = NULL;
        const int status = pacUnrollOneLoop(current ? current : source, &unrolled, reason, reasonSize);
        if (status < 0)
        {
            free(current);
            return NULL;
        }
        if (status == 0)
            break;
        free(current);
        current = unrolled;
    }
    char *translated = pacTranslateNvProgramCore(current ? current : source, reason, reasonSize);
    free(current);
    return translated;
}

static char *pacTranslateNvProgramCore(const char *source, char *reason, size_t reasonSize)
{
    static const char *const unsupported[] = {"vertex.id",     "vertex.instance", "fragment.attrib",
                                              "result.attrib", "program.buffer",  "result.layer",
                                              "result.clip",   "primitive.",      NULL};
    PacTextBuffer out = {NULL, 0, 0};
    int sawHeader = 0;
    int fragment = 0;
    int tempsDeclared = 0;
    unsigned char integerMarks[16] = {0};
    /* Flow control: IF/ELSE/ENDIF are converted to masked blends (fragment only). */
    int ifDepth = 0;
    int haveCondition = 0;
    char conditionMask[5] = "";
    char outputNames[16][32];
    int outputCount = 0;
    reason[0] = '\0';

    for (const char *const *word = unsupported; *word; ++word)
    {
        if (strstr(source, *word))
        {
            snprintf(reason, reasonSize, "uses %s", *word);
            return NULL;
        }
    }

    const char *cursor = source;
    while (*cursor)
    {
        const char *lineEnd = strchr(cursor, '\n');
        size_t rawLength = lineEnd ? (size_t)(lineEnd - cursor) : strlen(cursor);
        char line[1024];
        if (rawLength >= sizeof(line))
        {
            snprintf(reason, reasonSize, "line longer than %zu characters", sizeof(line) - 1);
            goto fail;
        }
        memcpy(line, cursor, rawLength);
        line[rawLength] = '\0';
        cursor = lineEnd ? lineEnd + 1 : cursor + rawLength;

        while (rawLength > 0 && (line[rawLength - 1] == '\r' || line[rawLength - 1] == ' ' ||
                                 line[rawLength - 1] == '\t'))
            line[--rawLength] = '\0';
        const char *p = line;
        while (*p == ' ' || *p == '\t')
            ++p;

        if (*p == '\0' || *p == '#')
        {
            if (!pacTextAppend(&out, line, rawLength) || !pacTextAppend(&out, "\n", 1))
                goto fail;
            continue;
        }

        if (!sawHeader)
        {
            if (strcmp(p, "!!NVvp4.0") == 0)
                fragment = 0;
            else if (strcmp(p, "!!NVfp4.0") == 0)
                fragment = 1;
            else
                goto notNvidia;
            sawHeader = 1;
            const char *header = fragment ? "!!ARBfp1.0\n" : "!!ARBvp1.0\n";
            if (!pacTextAppend(&out, header, strlen(header)))
                goto fail;
            continue;
        }

        /* gp4 predication: "OP dst(NE.x), ..." or "(EQ)" writes only where the condition holds.
         * The optional swizzle picks, for each result component, which condition-code
         * component to test; no swizzle means xyzw. */
        int hasPredicate = 0;
        int predicateNotEqual = 0;
        char predicateSwizzle[5] = "xyzw";
        {
            char *open = strchr(line, '(');
            if (open && *p != '#')
            {
                char *close = strchr(open, ')');
                const size_t length = close ? (size_t)(close - open - 1) : 0;
                char content[16];
                if (!close || length < 2 || length >= sizeof(content))
                {
                    snprintf(reason, reasonSize, "unsupported conditional execution: %.60s", p);
                    goto fail;
                }
                memcpy(content, open + 1, length);
                content[length] = '\0';
                const int notEqual = content[0] == 'N' && content[1] == 'E';
                const int equal = content[0] == 'E' && content[1] == 'Q';
                int valid = notEqual || equal;
                size_t letterCount = 4;
                const char *letters = "xyzw";
                if (valid && length > 2)
                {
                    letters = content + 3;
                    letterCount = length >= 4 ? length - 3 : 0;
                    valid = content[2] == '.' && letterCount > 0 &&
                            strspn(letters, "xyzw") == letterCount &&
                            (letterCount == 1 || letterCount == 4);
                }
                if (!valid)
                {
                    snprintf(reason, reasonSize, "unsupported condition-code test (%s): %.60s", content, p);
                    goto fail;
                }
                for (size_t i = 0; i < 4; ++i)
                    predicateSwizzle[i] = letterCount == 1 ? letters[0] : letters[i];
                predicateSwizzle[4] = '\0';
                hasPredicate = 1;
                predicateNotEqual = notEqual;
                memmove(open, close + 1, strlen(close + 1) + 1);
            }
        }

        char opcode[16];
        size_t n = 0;
        while ((isalnum((unsigned char)p[n]) || p[n] == '_') && n < sizeof(opcode) - 1)
        {
            opcode[n] = p[n];
            ++n;
        }
        opcode[n] = '\0';
        if (n == 0)
        {
            snprintf(reason, reasonSize, "unrecognised line: %.60s", p);
            goto fail;
        }

        static const char *const declarations[] = {"PARAM", "ATTRIB",  "TEMP",
                                                   "OUTPUT", "ALIAS", "ADDRESS", NULL};
        if (pacWordIn(opcode, declarations))
        {
            if (strcmp(opcode, "OUTPUT") == 0 && outputCount < 16)
            {
                const char *nameStart = p + n;
                while (*nameStart == ' ' || *nameStart == '\t')
                    ++nameStart;
                size_t nameLength = 0;
                while ((isalnum((unsigned char)nameStart[nameLength]) || nameStart[nameLength] == '_') &&
                       nameLength < sizeof(outputNames[0]) - 1)
                {
                    outputNames[outputCount][nameLength] = nameStart[nameLength];
                    ++nameLength;
                }
                outputNames[outputCount][nameLength] = '\0';
                if (nameLength > 0)
                    ++outputCount;
            }
            /* gp4 condition-code registers have no ARB counterpart; unused here. */
            if (strcmp(p, "TEMP RC, HC;") == 0 || strcmp(p, "TEMP RC;") == 0 ||
                strcmp(p, "TEMP HC;") == 0)
                continue;
            /* Array ATTRIB/OUTPUT declarations ("name[] = {...}") are NVIDIA-only.
             * Drop them when unused; PARAM arrays are fine in ARB and kept. */
            if ((strcmp(opcode, "ATTRIB") == 0 || strcmp(opcode, "OUTPUT") == 0) &&
                strstr(p, "[]"))
            {
                const char *nameStart = p + n;
                while (*nameStart == ' ' || *nameStart == '\t')
                    ++nameStart;
                char name[64];
                size_t nameLength = 0;
                while ((isalnum((unsigned char)nameStart[nameLength]) || nameStart[nameLength] == '_') &&
                       nameLength < sizeof(name) - 1)
                {
                    name[nameLength] = nameStart[nameLength];
                    ++nameLength;
                }
                name[nameLength] = '\0';
                if (nameLength > 0 && strstr(cursor, name))
                {
                    snprintf(reason, reasonSize, "uses array declaration %s", name);
                    goto fail;
                }
                continue;
            }
            if (!pacTextAppend(&out, p, strlen(p)) || !pacTextAppend(&out, "\n", 1))
                goto fail;
            continue;
        }

        if (strcmp(opcode, "OPTION") == 0)
        {
            if (strstr(p, "NV_gpu_program4"))
                continue;
            /* ARB options (ARB_draw_buffers, ARB_precision_hint_*, ARB_fog_*,
             * ARB_position_invariant, ...) are standard ARB assembly. Pass through. */
            if (strncmp(p, "OPTION ARB_", 11) == 0)
            {
                if (!pacTextAppend(&out, p, strlen(p)) || !pacTextAppend(&out, "\n", 1))
                    goto fail;
                continue;
            }
            snprintf(reason, reasonSize, "needs %.60s", p);
            goto fail;
        }

        /*
         * "TRUNC.U.CC HC.x, v;" followed by "IF NE.x" tests whether trunc(v) is
         * non-zero, i.e. v >= 1 (negative floats convert to 0). Remember v here.
         */
        if (strcmp(opcode, "TRUNC") == 0 && strncmp(p + n, ".U.CC", 5) == 0 &&
            !isalnum((unsigned char)p[n + 5]))
        {
            char split[256];
            char *parts[4];
            if (strlen(p + n + 5) >= sizeof(split))
            {
                snprintf(reason, reasonSize, "TRUNC.U.CC operands too long");
                goto fail;
            }
            memcpy(split, p + n + 5, strlen(p + n + 5) + 1);
            PacOperand source;
            if (pacSplitOperands(split, parts, 4) != 2 ||
                (strncmp(parts[0], "HC", 2) != 0 && strncmp(parts[0], "RC", 2) != 0) ||
                !pacParseOperand(parts[1], &source))
            {
                snprintf(reason, reasonSize, "unsupported condition-code instruction: %.60s", p);
                goto fail;
            }
            const char *maskStart = strchr(parts[0], '.');
            snprintf(conditionMask, sizeof(conditionMask), "%s", maskStart ? maskStart + 1 : "xyzw");
            if (strspn(conditionMask, "xyzw") != strlen(conditionMask) || conditionMask[0] == '\0')
            {
                snprintf(reason, reasonSize, "unsupported condition-code mask: %.60s", p);
                goto fail;
            }
            /*
             * Capture now, as the CC register does: pacCC.c = (value >= 1) = (trunc(value) != 0).
             * Inside an IF only the active lanes update the condition, as on the hardware.
             */
            if (!haveCondition && !pacEmitf(&out, "MOV pacCC, {0}.x;\n"))
                goto fail;
            const size_t sourceSwizzle = strlen(source.swizzle);
            for (size_t k = 0; k < strlen(conditionMask); ++k)
            {
                static const char components[] = "xyzw";
                char letter;
                if (sourceSwizzle == 0)
                    letter = components[k < 4 ? k : 0];
                else if (sourceSwizzle == 1)
                    letter = source.swizzle[0];
                else
                    letter = source.swizzle[k < sourceSwizzle ? k : 0];
                int ok = pacEmitf(&out, "MOV pacT1.x, %s%s.%c;\n", source.sign, source.base, letter);
                if (ok && ifDepth == 0)
                    ok = pacEmitf(&out, "SGE pacCC.%c, pacT1.x, {1}.x;\n", conditionMask[k]);
                else if (ok)
                    ok = pacEmitf(&out, "SGE pacV, pacT1.x, {1}.x;\n") &&
                         pacEmitf(&out, "SUB pacD, pacV, pacCC;\n") &&
                         pacEmitf(&out, "MAD pacCC.%c, pacM%d.x, pacD, pacCC;\n", conditionMask[k],
                                  ifDepth - 1);
                if (!ok)
                    goto fail;
            }
            tempsDeclared |= 1 | 8;
            tempsDeclared |= 16;
            haveCondition = 1;
            continue;
        }

        if (strcmp(opcode, "IF") == 0 || strcmp(opcode, "ELSE") == 0 ||
            strcmp(opcode, "ENDIF") == 0)
        {
            if (strcmp(opcode, "ENDIF") == 0)
            {
                if (ifDepth == 0)
                {
                    snprintf(reason, reasonSize, "ENDIF without IF");
                    goto fail;
                }
                --ifDepth;
                continue;
            }
            if (strcmp(opcode, "ELSE") == 0)
            {
                if (ifDepth == 0)
                {
                    snprintf(reason, reasonSize, "ELSE without IF");
                    goto fail;
                }
                const int level = ifDepth - 1;
                int ok = pacEmitf(&out, "ADD pacM%d, -pacK%d, {1}.x;\n", level, level);
                if (ok && level > 0)
                    ok = pacEmitf(&out, "MUL pacM%d, pacM%d, pacM%d;\n", level, level, level - 1);
                if (!ok)
                    goto fail;
                continue;
            }

            /* IF NE.c / IF EQ.c */
            const char *test = p + n;
            while (*test == ' ' || *test == '\t')
                ++test;
            const int testNotEqual = test[0] == 'N' && test[1] == 'E';
            const int testEqual = test[0] == 'E' && test[1] == 'Q';
            if (!(testNotEqual || testEqual) || test[2] != '.' || !strchr("xyzw", test[3]))
            {
                snprintf(reason, reasonSize, "unsupported IF condition: %.60s", p);
                goto fail;
            }
            if (!haveCondition)
            {
                snprintf(reason, reasonSize, "IF without a TRUNC.U.CC condition");
                goto fail;
            }
            const char *position = strchr(conditionMask, test[3]);
            if (!position)
            {
                snprintf(reason, reasonSize, "IF tests a component the condition did not set");
                goto fail;
            }
            if (ifDepth >= 4)
            {
                snprintf(reason, reasonSize, "IF nested deeper than 4");
                goto fail;
            }
            (void)position;
            const int level = ifDepth;
            int ok = testNotEqual ? pacEmitf(&out, "MOV pacK%d, pacCC.%c;\n", level, test[3])
                                  : pacEmitf(&out, "ADD pacK%d, -pacCC.%c, {1}.x;\n", level, test[3]);
            if (ok)
                ok = level == 0 ? pacEmitf(&out, "MOV pacM0, pacK0;\n")
                                : pacEmitf(&out, "MUL pacM%d, pacM%d, pacK%d;\n", level, level - 1, level);
            if (!ok)
                goto fail;
            tempsDeclared |= 32 | 16;
            ++ifDepth;
            continue;
        }

        if (strchr(p, '('))
        {
            snprintf(reason, reasonSize, "conditional execution: %.60s", p);
            goto fail;
        }

        static const char *const expandable[] = {"DIV", "SGT", "SLE", "SEQ", "SNE", "DP2", "CEIL", "TRUNC", "I2F", NULL};
        const int needsExpansion = pacWordIn(opcode, expandable);

        if (!needsExpansion && strcmp(opcode, "END") != 0 && !pacOpcodeAllowed(opcode, fragment))
        {
            snprintf(reason, reasonSize, "instruction %s has no ARB equivalent here", opcode);
            goto fail;
        }

        const char *rest = p + n;
        int saturate = 0;
        char integerKind = 0; /* 'U' or 'S' on TRUNC: the result is an integer */
        while (*rest == '.')
        {
            const char *start = rest + 1;
            size_t length = 0;
            while (isalnum((unsigned char)start[length]) || start[length] == '_')
                ++length;
            if (length == 1 && start[0] == 'F')
                ;
            else if (length == 3 && strncmp(start, "SAT", 3) == 0)
                saturate = 1;
            else if (length == 1 && (start[0] == 'U' || start[0] == 'S') && strcmp(opcode, "TRUNC") == 0)
                integerKind = start[0];
            else if (length == 1 && (start[0] == 'U' || start[0] == 'S') && strcmp(opcode, "MOV") == 0)
                integerKind = start[0];
            else if (length == 1 && (start[0] == 'U' || start[0] == 'S') && strcmp(opcode, "I2F") == 0)
                ;
            else
            {
                snprintf(reason, reasonSize, "modifier .%.*s on %.20s", (int)(length < 20 ? length : 20), start, opcode);
                goto fail;
            }
            rest = start + length;
        }

        if (integerKind == 'U')
            strcpy(opcode, "TRUNCU");

        /* Integer tracking: see pacReadsInteger. */
        const int isIntegerResult = integerKind != 0;
        const int isI2F = strcmp(opcode, "I2F") == 0;
        int integerRegister = -1;
        unsigned integerBits = 0;
        {
            char text[512];
            char *parts[8];
            if (strlen(rest) >= sizeof(text))
            {
                snprintf(reason, reasonSize, "%s operands too long", opcode);
                goto fail;
            }
            size_t length = 0;
            for (const char *c = rest; *c; ++c)
                if (*c != '|')
                    text[length++] = *c;
            text[length] = '\0';
            const int partCount = pacSplitOperands(text, parts, 8);
            PacOperand destination;
            const char *mask = "xyzw";
            if (partCount >= 1 && pacParseOperand(parts[0], &destination))
            {
                integerRegister = pacRegisterIndex(destination.base);
                mask = destination.swizzle[0] ? destination.swizzle : "xyzw";
                for (const char *c = mask; *c; ++c)
                    integerBits |= pacLetterBit(*c);
            }
            if ((isIntegerResult || isI2F) && (ifDepth > 0 || hasPredicate))
            {
                snprintf(reason, reasonSize, "%s inside IF or under a predicate", opcode);
                goto fail;
            }
            if (isIntegerResult && strcmp(opcode, "MOV") == 0 && (partCount != 2 || parts[1][0] != '{'))
            {
                snprintf(reason, reasonSize, "integer MOV from something other than a constant: %.60s", p);
                goto fail;
            }
            if (isIntegerResult && integerRegister < 0)
            {
                snprintf(reason, reasonSize, "integer result written to a non-temporary: %.60s", p);
                goto fail;
            }
            if (isI2F)
            {
                PacOperand source;
                const int sourceIndex =
                    (partCount == 2 && pacParseOperand(parts[1], &source)) ? pacRegisterIndex(source.base) : -1;
                if (sourceIndex < 0 ||
                    (pacLettersAt(&source, pacMaskPositions(mask)) & ~(unsigned)integerMarks[sourceIndex]))
                {
                    snprintf(reason, reasonSize, "I2F of a value that is not a known integer: %.60s", p);
                    goto fail;
                }
            }
            else
            {
                char which[48];
                if (pacReadsInteger(rest, integerMarks, opcode, which, sizeof(which)))
                {
                    snprintf(reason, reasonSize, "integer held in %s is read by %s (only I2F is supported)",
                             which, opcode);
                    goto fail;
                }
            }
        }

        /*
         * ARB vertex programs have no _SAT suffix (fragment programs do). Run the
         * instruction into a temp and clamp it into the real destination.
         */
        char work[512];
        char clampDestination[128];
        char predicatedDestination[128];
        char maskName[16] = "";
        char guardMask[24] = "";
        int clampAfter = 0;
        int predicated = 0;
        if (strlen(rest) >= sizeof(work))
        {
            snprintf(reason, reasonSize, "%s operands too long", opcode);
            goto fail;
        }
        memcpy(work, rest, strlen(rest) + 1);

        if (!pacExtractAbs(&out, work, sizeof(work), &tempsDeclared, reason, reasonSize))
            goto fail;

        /* Inside IF/ELSE or under a predicate, compute into a temp and blend it in. */
        if (ifDepth > 0 || hasPredicate)
        {
            char split[512];
            char *parts[8];
            memcpy(split, work, strlen(work) + 1);
            const int partCount = pacSplitOperands(split, parts, 8);
            PacOperand destination;
            if (partCount < 2 || strlen(parts[0]) >= sizeof(predicatedDestination) ||
                !pacParseOperand(parts[0], &destination))
            {
                snprintf(reason, reasonSize, "%s inside IF has no writable destination", opcode);
                goto fail;
            }
            int isOutput = strncmp(destination.base, "result.", 7) == 0;
            for (int i = 0; i < outputCount && !isOutput; ++i)
                isOutput = strcmp(destination.base, outputNames[i]) == 0;
            if (isOutput)
            {
                snprintf(reason, reasonSize, "write to %s inside IF", destination.base);
                goto fail;
            }
            memcpy(predicatedDestination, parts[0], strlen(parts[0]) + 1);

            if (ifDepth > 0)
            {
                snprintf(maskName, sizeof(maskName), "pacM%d", ifDepth - 1);
                snprintf(guardMask, sizeof(guardMask), "pacM%d.x", ifDepth - 1);
            }
            if (hasPredicate)
            {
                static const char xyzw[] = "xyzw";
                const char *destinationMask = destination.swizzle[0] ? destination.swizzle : xyzw;
                int ok = pacEmitf(&out, "MOV pacP, {0}.x;\n");
                char firstTest = 0;
                int sameTest = 1;
                for (const char *c = destinationMask; ok && *c; ++c)
                {
                    const char *position = strchr(xyzw, *c);
                    if (!position)
                    {
                        snprintf(reason, reasonSize, "unsupported write mask on a predicated instruction");
                        goto fail;
                    }
                    const char test = predicateSwizzle[position - xyzw];
                    if (!haveCondition || !strchr(conditionMask, test))
                    {
                        snprintf(reason, reasonSize, "predicate tests a condition that was not set");
                        goto fail;
                    }
                    if (!firstTest)
                        firstTest = test;
                    else if (test != firstTest)
                        sameTest = 0;
                    ok = predicateNotEqual ? pacEmitf(&out, "MOV pacP.%c, pacCC.%c;\n", *c, test)
                                           : pacEmitf(&out, "ADD pacP.%c, -pacCC.%c, {1}.x;\n", *c, test);
                }
                if (ok && ifDepth > 0)
                    ok = pacEmitf(&out, "MUL pacP, pacP, pacM%d;\n", ifDepth - 1);
                if (!ok)
                    goto fail;
                snprintf(maskName, sizeof(maskName), "pacP");
                /* One scalar mask is needed to guard divisions; only valid if all components agree. */
                if (sameTest)
                    snprintf(guardMask, sizeof(guardMask), "pacP.%c", destinationMask[0]);
                else
                    guardMask[0] = '\0';
                tempsDeclared |= 16;
            }

            /*
             * A branch that is not taken still computes its instructions here. If a lane
             * that is masked out divides by zero, the infinity times a zero mask is NaN and
             * poisons the result. So the operand that can blow up is replaced by 1 for
             * lanes where this instruction is not active: g = 1 + mask * (operand - 1).
             */
            if (strcmp(opcode, "RCP") == 0 || strcmp(opcode, "RSQ") == 0 ||
                strcmp(opcode, "LG2") == 0 || strcmp(opcode, "EX2") == 0 ||
                strcmp(opcode, "POW") == 0)
            {
                if (partCount < 2 || !guardMask[0])
                {
                    snprintf(reason, reasonSize, "%s under a multi-condition predicate", opcode);
                    goto fail;
                }
                if (!(pacEmitf(&out, "MOV pacT1.x, %s;\n", parts[1]) &&
                      pacEmitf(&out, "SUB pacT1.x, pacT1.x, {1}.x;\n") &&
                      pacEmitf(&out, "MAD pacT1.x, %s, pacT1.x, {1}.x;\n", guardMask)))
                    goto fail;
                parts[1] = (char *)"pacT1.x";
                tempsDeclared |= 1;
            }

            size_t used = (size_t)snprintf(work, sizeof(work), " pacV");
            for (int i = 1; i < partCount; ++i)
            {
                const int added = snprintf(work + used, sizeof(work) - used, ", %s", parts[i]);
                if (added < 0 || (size_t)added >= sizeof(work) - used)
                {
                    snprintf(reason, reasonSize, "%s operands too long", opcode);
                    goto fail;
                }
                used += (size_t)added;
            }
            if (used + 2 > sizeof(work))
            {
                snprintf(reason, reasonSize, "%s operands too long", opcode);
                goto fail;
            }
            work[used++] = ';';
            work[used] = '\0';
            predicated = 1;
            tempsDeclared |= 8;
        }

        /*
         * ARB vertex programs have no _SAT suffix (fragment programs do). Run the
         * instruction into a temp and clamp it into the real destination.
         */
        if (saturate && !fragment)
        {
            char split[512];
            char *parts[8];
            memcpy(split, work, strlen(work) + 1);
            const int partCount = pacSplitOperands(split, parts, 8);
            if (partCount < 2 || strlen(parts[0]) >= sizeof(clampDestination))
            {
                snprintf(reason, reasonSize, "cannot saturate %s", opcode);
                goto fail;
            }
            memcpy(clampDestination, parts[0], strlen(parts[0]) + 1);

            size_t used = (size_t)snprintf(work, sizeof(work), " pacS");
            for (int i = 1; i < partCount; ++i)
            {
                const int added = snprintf(work + used, sizeof(work) - used, ", %s", parts[i]);
                if (added < 0 || (size_t)added >= sizeof(work) - used)
                {
                    snprintf(reason, reasonSize, "%s operands too long", opcode);
                    goto fail;
                }
                used += (size_t)added;
            }
            if (used + 2 > sizeof(work))
            {
                snprintf(reason, reasonSize, "%s operands too long", opcode);
                goto fail;
            }
            work[used++] = ';';
            work[used] = '\0';
            saturate = 0;
            clampAfter = 1;
            tempsDeclared |= 2;
        }

        if (needsExpansion)
        {
            if (pacEmitExpanded(&out, opcode, saturate, work, &tempsDeclared,
                                predicated ? guardMask : NULL, reason, reasonSize) < 0)
                goto fail;
        }
        else if (!pacTextAppend(&out, opcode, strlen(opcode)) ||
                 (saturate && !pacTextAppend(&out, "_SAT", 4)) ||
                 !pacTextAppend(&out, work, strlen(work)) || !pacTextAppend(&out, "\n", 1))
            goto fail;

        if (clampAfter &&
            !(pacEmitf(&out, "MAX pacS, pacS, {0}.x;\n") &&
              pacEmitf(&out, "MIN %s, pacS, {1}.x;\n", clampDestination)))
            goto fail;

        if (predicated)
        {
            PacOperand destination;
            pacParseOperand(predicatedDestination, &destination);
            if (!(pacEmitf(&out, "SUB pacD, pacV, %s;\n", destination.base) &&
                  pacEmitf(&out, "MAD %s, %s%s, pacD, %s;\n", predicatedDestination, maskName,
                           strcmp(maskName, "pacP") == 0 ? "" : ".x", destination.base)))
                goto fail;
        }

        if (integerRegister >= 0)
        {
            integerMarks[integerRegister] &= (unsigned char)~integerBits;
            if (isIntegerResult)
                integerMarks[integerRegister] |= (unsigned char)integerBits;
        }
    }

    if (!sawHeader)
        goto notNvidia;

    if (ifDepth != 0)
    {
        snprintf(reason, reasonSize, "IF without ENDIF");
        goto fail;
    }

    if (tempsDeclared)
    {
        char declaration[400] = "";
        if (tempsDeclared & 1)
            strcat(declaration, "TEMP pacT0, pacT1;\n");
        if (tempsDeclared & 2)
            strcat(declaration, "TEMP pacS;\n");
        if (tempsDeclared & 4)
            strcat(declaration, "TEMP pacA0, pacA1, pacA2, pacA3;\n");
        if (tempsDeclared & 16)
            strcat(declaration, "TEMP pacCC, pacP;\n");
        if (tempsDeclared & 8)
            strcat(declaration, "TEMP pacV, pacD;\n");
        if (tempsDeclared & 32)
            strcat(declaration, "TEMP pacK0, pacK1, pacK2, pacK3, pacM0, pacM1, pacM2, pacM3;\n");
        const char *headerEnd = out.data ? strchr(out.data, '\n') : NULL;
        if (headerEnd)
        {
            /* OPTION lines must precede every declaration: insert after them. */
            size_t insertOffset = (size_t)(headerEnd - out.data) + 1;
            if (insertOffset > out.length)
                goto fail;
            for (;;)
            {
                const char *q = out.data + insertOffset;
                while (*q == ' ' || *q == '\t')
                    ++q;
                if (*q == '#' || *q == '\n' || strncmp(q, "OPTION", 6) == 0)
                {
                    const char *newline = strchr(out.data + insertOffset, '\n');
                    if (!newline)
                        break;
                    insertOffset = (size_t)(newline - out.data) + 1;
                    if (insertOffset > out.length)
                        goto fail;
                    continue;
                }
                break;
            }
            PacTextBuffer fixed = {NULL, 0, 0};
            if (!pacTextAppend(&fixed, out.data, insertOffset) ||
                !pacTextAppend(&fixed, declaration, strlen(declaration)) ||
                !pacTextAppend(&fixed, out.data + insertOffset, out.length - insertOffset))
            {
                free(fixed.data);
                goto fail;
            }
            free(out.data);
            return fixed.data;
        }
    }
    return out.data;

notNvidia:
    free(out.data);
    return NULL;
fail:
    if (reason[0] == '\0')
        snprintf(reason, reasonSize, "out of memory");
    free(out.data);
    return NULL;
}


/*
 * Last resort for a fragment program that cannot be translated. Failing the load stops the whole
 * game, so the program is replaced by a valid pass-through one: the comment lines (which carry the
 * #var / #semantic annotations the Cg runtime reads to build its parameter list) are kept, and the
 * code just shows the texture at unit 0 (right for most screen effects) or the interpolated colour.
 * The effect will not look right, but everything else can keep running. Returns malloc'd text.
 */
static char *pacBuildFragmentFallback(const char *original)
{
    static const char header[] = "!!ARBfp1.0\n";
    static const char sampleBody[] =
        "TEMP pacFallback;\nTEX pacFallback, fragment.texcoord[0], texture[0], 2D;\n"
        "MOV result.color, pacFallback;\nEND\n";
    static const char colourBody[] = "MOV result.color, fragment.color;\nEND\n";

    PacTextBuffer out = {NULL, 0, 0};
    int ok = pacTextAppend(&out, header, sizeof(header) - 1);
    const char *cursor = original;
    while (ok && *cursor)
    {
        const char *end = strchr(cursor, '\n');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        const char *text = cursor;
        while (length > 0 && (*text == ' ' || *text == '\t'))
        {
            ++text;
            --length;
        }
        while (length > 0 && (text[length - 1] == '\r' || text[length - 1] == ' '))
            --length;
        if (length > 0 && *text == '#')
            ok = pacTextAppend(&out, text, length) && pacTextAppend(&out, "\n", 1);
        if (!end)
            break;
        cursor = end + 1;
    }
    const int samples = strstr(original, "texture[0]") && strstr(original, "fragment.texcoord[0]");
    if (ok)
        ok = samples ? pacTextAppend(&out, sampleBody, sizeof(sampleBody) - 1)
                     : pacTextAppend(&out, colourBody, sizeof(colourBody) - 1);
    if (!ok)
    {
        free(out.data);
        return NULL;
    }
    return out.data;
}

char *bridgeCgCreateProgram(uint32_t context, int programType, const char *program,
                            int profile, const char *entry, const char **args)
{
    typedef char *(*CgCreateProgram)(uint32_t, int, const char *, int,
                                     const char *, const char **);
    CgCreateProgram createProgram = (CgCreateProgram)real_cgCreateProgram;
    cgLogProgram(programType == 4112 ? "cgCreateProgram(source)" : "cgCreateProgram(object)",
                 profile, entry, program);

    /* NVIDIA drivers take the game's gp4 assembly as is; other GPUs need ARB. The
     * translated text is deliberately never freed: it is a few hundred bytes per
     * program and the Cg runtime's use of the pointer is not ours to second-guess. */
    const char *programText = program;
    if (program && getConfig()->GPUVendor != NVIDIA_GPU && strncmp(program, "!!NV", 4) == 0)
    {
        char reason[160];
        char *translated = pacTranslateNvProgram(program, reason, sizeof(reason));
        if (translated)
        {
            programText = translated;
            cgLogProgram("translated to ARB", profile, entry, translated);
        }
        else
        {
            log_error("Cg: cannot translate this NVIDIA program for this GPU: %s",
                      reason[0] ? reason : "not recognised");

            /* Always keep the full text of a program we could not translate: the
             * general log stops after 200 entries, and this is the one that matters. */
            FILE *untranslated = fopen("pacloader_cg_untranslated.txt", "ab");
            if (untranslated)
            {
                fprintf(untranslated, "==== UNTRANSLATABLE profile=%d entry=%s reason=%s\n%s\n==== end\n\n",
                        profile, entry ? entry : "(none)", reason[0] ? reason : "not recognised",
                        program);
                fclose(untranslated);
            }

            /* A fragment program that cannot be translated is replaced by a pass-through one so
             * the rest of the game can keep going. The effect itself will look wrong. */
            if (cgFallbackEnabled() && strncmp(program, "!!NVfp4.0", 9) == 0)
            {
                char *fallback = pacBuildFragmentFallback(program);
                if (fallback)
                {
                    char source[128] = "(unknown)";
                    const char *marker = strstr(program, "# source file: ");
                    if (marker)
                    {
                        marker += 15;
                        size_t n = 0;
                        while (marker[n] && marker[n] != '\n' && marker[n] != '\r' && n < sizeof(source) - 1)
                        {
                            source[n] = marker[n];
                            ++n;
                        }
                        source[n] = '\0';
                    }
                    log_error("Cg: FALLBACK - %s is replaced by a pass-through program; this effect will not "
                              "render correctly (set LL_CG_FALLBACK=0 to disable)", source);
                    programText = fallback;
                }
            }
        }
    }
    return createProgram ? createProgram(context, programType, programText, profile, entry, args) : NULL;
}

char *bridgeCgGetProgramString(char *program, int parameter)
{
    typedef char *(*CgGetProgramString)(char *, int);
    CgGetProgramString getProgramString = (CgGetProgramString)real_cgGetProgramString;
    char *result = getProgramString ? getProgramString(program, parameter) : NULL;
    if (parameter == 4106) /* CG_COMPILED_PROGRAM */
        cgLogProgram("cgGetProgramString(compiled)", -1, NULL, result);
    return result;
}

void bridgeglEnable(GLenum capability)
{
    /* This, not wrap_glEnable, is what the guest's glEnable resolves to, so the
     * hooks' shadow of GL_SCISSOR_TEST has to be fed from here. */
    GLHooks_NotifyCapToggled(capability, 1);
    if (glad_glEnable)
        glad_glEnable(capability);
}

void bridgeglDisable(GLenum capability)
{
    GLHooks_NotifyCapToggled(capability, 0);
    if (glad_glDisable)
        glad_glDisable(capability);
}

void bridgeglBindTexture(GLenum target, GLuint texture)
{
    if (glad_glBindTexture)
        glad_glBindTexture(target, texture);
    GLHooks_NotifyTextureBinding(target, texture);
}

/* NV_fence is absent on many AMD and Intel drivers.  Present the NV API the
 * guest expects while backing it with core OpenGL sync objects. */
#define PACLOADER_MAX_FENCES 256

typedef struct
{
    GLuint id;
    GLsync sync;
} PacloaderFence;

static PacloaderFence fences[PACLOADER_MAX_FENCES];
static GLuint nextFenceId = 1;

void pacGraphicsReportCapabilities(void)
{
    const char *vendor = (const char *)glad_glGetString(GL_VENDOR);
    const char *renderer = (const char *)glad_glGetString(GL_RENDERER);
    const char *version = (const char *)glad_glGetString(GL_VERSION);
    log_info("OpenGL compatibility: vendor=%s renderer=%s version=%s",
             vendor ? vendor : "unknown", renderer ? renderer : "unknown",
             version ? version : "unknown");
    log_info("OpenGL compatibility: NV_fence=%s, core sync=%s, S3TC=%s",
             glad_glGenFencesNV ? "native" : "emulated",
             (glad_glFenceSync && glad_glClientWaitSync) ? "yes" : "no",
             GLAD_GL_EXT_texture_compression_s3tc ? "yes" : "no");

    if (!glad_glFenceSync && !glad_glGenFencesNV)
        log_warn("OpenGL compatibility: this driver has neither NV_fence nor core sync support");
}

/*
 * ARB assembly programs (from the game's Cg runtime) are rejected by the driver
 * with a bare GL_INVALID_OPERATION. Wrap the glad entry point, which every path
 * to the driver goes through, so the driver's message, the error position and
 * the program text are logged and appended to pacloader_shader_errors.txt.
 */
#define PAC_PROGRAM_ERROR_POSITION_ARB 0x864B
#define PAC_PROGRAM_ERROR_STRING_ARB 0x8874

static PFNGLPROGRAMSTRINGARBPROC realProgramStringARB = NULL;
static volatile LONG programErrorsReported = 0;

static void APIENTRY diagProgramStringARB(GLenum target, GLenum format, GLsizei length,
                                          const void *program)
{
    realProgramStringARB(target, format, length, program);

    GLint position = -1;
    glad_glGetIntegerv(PAC_PROGRAM_ERROR_POSITION_ARB, &position);
    const GLenum glError = glad_glGetError();
    if (position < 0 && glError == GL_NO_ERROR)
        return; /* accepted */

    if (InterlockedIncrement(&programErrorsReported) > 12)
        return;

    const char *message = (const char *)glad_glGetString(PAC_PROGRAM_ERROR_STRING_ARB);
    const char *text = (const char *)program;
    const int textLength = (text && length > 0) ? (int)length : 0;

    log_error("ARB program rejected: target=0x%04X format=0x%04X length=%d errorPosition=%d glError=0x%04X",
              target, format, (int)length, (int)position, glError);
    log_error("Driver message: %s", message ? message : "(none)");

    if (textLength > 0)
    {
        int start = (position >= 0 && position < textLength) ? position : 0;
        int end = start;
        while (start > 0 && text[start - 1] != '\n')
            --start;
        while (end < textLength && text[end] != '\n')
            ++end;
        log_error("Offending line: %.*s", end - start, text + start);
        log_error("Program starts: %.*s", textLength < 160 ? textLength : 160, text);

        FILE *file = fopen("pacloader_shader_errors.txt", "ab");
        if (file)
        {
            fprintf(file,
                    "==== rejected program: target=0x%04X length=%d errorPosition=%d glError=0x%04X\n"
                    "==== driver message: %s\n",
                    target, (int)length, (int)position, glError, message ? message : "(none)");
            fwrite(text, 1, (size_t)textLength, file);
            fputs("\n==== end\n\n", file);
            fclose(file);
        }
    }
}

void pacInstallShaderDiagnostics(void)
{
    if (!glad_glProgramStringARB)
    {
        log_warn("Shader diagnostics: this driver has no glProgramStringARB (ARB_vertex/fragment_program)");
        return;
    }
    if (realProgramStringARB)
        return; /* already installed */
    realProgramStringARB = glad_glProgramStringARB;
    glad_glProgramStringARB = diagProgramStringARB;
    log_warn("Shader diagnostics: wrapping glProgramStringARB");
}

static int useNativeNvFences(void)
{
    return getConfig()->GPUVendor == NVIDIA_GPU && glad_glGenFencesNV &&
           glad_glDeleteFencesNV && glad_glSetFenceNV && glad_glTestFenceNV &&
           glad_glFinishFenceNV && glad_glIsFenceNV;
}

static PacloaderFence *findFence(GLuint id)
{
    for (size_t i = 0; i < PACLOADER_MAX_FENCES; ++i)
    {
        if (fences[i].id == id)
            return &fences[i];
    }
    return NULL;
}

static PacloaderFence *allocateFence(GLuint id)
{
    for (size_t i = 0; i < PACLOADER_MAX_FENCES; ++i)
    {
        if (fences[i].id == 0)
        {
            fences[i].id = id;
            fences[i].sync = NULL;
            return &fences[i];
        }
    }
    return NULL;
}

void bridgeglGenFencesNV(GLsizei count, GLuint *output)
{
    if (useNativeNvFences())
    {
        glad_glGenFencesNV(count, output);
        return;
    }

    for (GLsizei i = 0; i < count; ++i)
    {
        GLuint id = nextFenceId++;
        if (id == 0)
            id = nextFenceId++;
        output[i] = allocateFence(id) ? id : 0;
    }
}

void bridgeglDeleteFencesNV(GLsizei count, const GLuint *ids)
{
    if (useNativeNvFences())
    {
        glad_glDeleteFencesNV(count, ids);
        return;
    }

    for (GLsizei i = 0; i < count; ++i)
    {
        PacloaderFence *fence = findFence(ids[i]);
        if (!fence)
            continue;
        if (fence->sync && glad_glDeleteSync)
            glad_glDeleteSync(fence->sync);
        fence->id = 0;
        fence->sync = NULL;
    }
}

void bridgeglSetFenceNV(GLuint id, GLenum condition)
{
    (void)condition;
    if (useNativeNvFences())
    {
        glad_glSetFenceNV(id, condition);
        return;
    }

    PacloaderFence *fence = findFence(id);
    if (!fence || !glad_glFenceSync)
        return;
    if (fence->sync && glad_glDeleteSync)
        glad_glDeleteSync(fence->sync);
    fence->sync = glad_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

GLboolean bridgeglTestFenceNV(GLuint id)
{
    if (useNativeNvFences())
        return glad_glTestFenceNV(id);

    PacloaderFence *fence = findFence(id);
    if (!fence || !fence->sync || !glad_glClientWaitSync)
        return GL_TRUE;
    GLenum status = glad_glClientWaitSync(fence->sync, 0, 0);
    return status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED;
}

void bridgeglFinishFenceNV(GLuint id)
{
    if (useNativeNvFences())
    {
        glad_glFinishFenceNV(id);
        return;
    }

    PacloaderFence *fence = findFence(id);
    if (fence && fence->sync && glad_glClientWaitSync)
        glad_glClientWaitSync(fence->sync, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
}

GLboolean bridgeglIsFenceNV(GLuint id)
{
    if (useNativeNvFences())
        return glad_glIsFenceNV(id);
    return findFence(id) ? GL_TRUE : GL_FALSE;
}

#endif
