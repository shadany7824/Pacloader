#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "iniParser.h"
#include "../log/log.h"

/* Trim leading and trailing whitespace in place. */
static char *trimString(char *str)
{
    char *end;
    while (isspace((unsigned char)*str))
        str++;
    if (*str == 0)
        return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;
    end[1] = '\0';
    return str;
}

/* Load an INI file; the caller owns the returned configuration. */
IniConfig *iniLoad(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        log_warn("Error opening INI file %s\n", filename);
        return NULL;
    }
    IniConfig *config = calloc(1, sizeof(IniConfig));
    if (!config)
    {
        fclose(file);
        return NULL;
    }
    char line[1024];
    IniSection *currentSection = NULL;
    while (fgets(line, sizeof(line), file))
    {
        char *trimmedLine = trimString(line);
        if (trimmedLine[0] == ';' || trimmedLine[0] == '#' || trimmedLine[0] == '\0')
            continue;
        if (trimmedLine[0] == '[' && trimmedLine[strlen(trimmedLine) - 1] == ']')
        {
            IniSection *newSections = realloc(config->sections, (config->numSections + 1) * sizeof(IniSection));
            if (!newSections)
            {
                iniFree(config);
                fclose(file);
                return NULL;
            }
            config->sections = newSections;
            currentSection = &config->sections[config->numSections++];
            currentSection->numPairs = 0;
            currentSection->pairs = NULL;
            char *sectionName = trimmedLine + 1;
            sectionName[strlen(sectionName) - 1] = '\0';
            currentSection->name = strdup(sectionName);
        }
        else if (currentSection && strchr(trimmedLine, '='))
        {
            char *key = trimmedLine;
            char *value = strchr(trimmedLine, '=');
            *value = '\0';
            value++;
            key = trimString(key);
            value = trimString(value);
            IniKeyValuePair *newPairs = realloc(currentSection->pairs, (currentSection->numPairs + 1) * sizeof(IniKeyValuePair));
            if (!newPairs)
            {
                iniFree(config);
                fclose(file);
                return NULL;
            }
            currentSection->pairs = newPairs;
            IniKeyValuePair *pair = &currentSection->pairs[currentSection->numPairs++];
            pair->key = strdup(key);
            pair->value = strdup(value);
        }
    }
    fclose(file);
    return config;
}

/* Find a section by name. */
IniSection *iniGetSection(const IniConfig *config, const char *sectionName)
{
    if (!config)
        return NULL;
    for (int i = 0; i < config->numSections; i++)
    {
        if (strcmp(config->sections[i].name, sectionName) == 0)
            return &config->sections[i];
    }
    return NULL;
}

/* Free an INI configuration and all owned strings. */
void iniFree(IniConfig *config)
{
    if (!config)
        return;
    for (int i = 0; i < config->numSections; i++)
    {
        for (int j = 0; j < config->sections[i].numPairs; j++)
        {
            free(config->sections[i].pairs[j].key);
            free(config->sections[i].pairs[j].value);
        }
        free(config->sections[i].pairs);
        free(config->sections[i].name);
    }
    free(config->sections);
    free(config);
}

/* Update a key or append it to the requested section. */
bool iniSetValue(IniConfig *config, const char *sectionName, const char *key, const char *value)
{
    if (!config || !sectionName || !key || !value)
    {
        return false;
    }

    IniSection *section = iniGetSection(config, sectionName);
    if (!section)
    {
        IniSection *newSections = realloc(config->sections, (config->numSections + 1) * sizeof(IniSection));
        if (!newSections)
            return false;
        config->sections = newSections;
        section = &config->sections[config->numSections++];
        section->name = strdup(sectionName);
        section->pairs = NULL;
        section->numPairs = 0;
    }

    for (int i = 0; i < section->numPairs; i++)
    {
        if (strcmp(section->pairs[i].key, key) == 0)
        {
            free(section->pairs[i].value);
            section->pairs[i].value = strdup(value);
            return true;
        }
    }

    IniKeyValuePair *newPairs = realloc(section->pairs, (section->numPairs + 1) * sizeof(IniKeyValuePair));
    if (!newPairs)
        return false;
    section->pairs = newPairs;
    IniKeyValuePair *pair = &section->pairs[section->numPairs++];
    pair->key = strdup(key);
    pair->value = strdup(value);
    return true;
}

/* Save the configuration, replacing any existing file. */
int iniSave(const IniConfig *config, const char *filename)
{
    if (!config || !filename)
    {
        return -1;
    }

    FILE *file = fopen(filename, "w");
    if (!file)
    {
        perror("Error saving INI file");
        return -1;
    }

    for (int i = 0; i < config->numSections; i++)
    {
        IniSection *section = &config->sections[i];
        fprintf(file, "[%s]\n", section->name);
        for (int j = 0; j < section->numPairs; j++)
        {
            IniKeyValuePair *pair = &section->pairs[j];
            fprintf(file, "%s = %s\n", pair->key, pair->value);
        }
        if (i < config->numSections - 1)
        {
            fprintf(file, "\n");
        }
    }

    fclose(file);
    return 1;
}

const char *iniGetValue(const IniConfig *config, const char *sectionName, const char *key)
{
    IniSection *section = iniGetSection(config, sectionName);
    if (!section)
        return NULL;

    for (int i = 0; i < section->numPairs; i++)
    {
        if (strcmp(section->pairs[i].key, key) == 0)
            return section->pairs[i].value;
    }

    return NULL;
}
