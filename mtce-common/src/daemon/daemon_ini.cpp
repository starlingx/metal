/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/* inih -- simple .INI file parser

inih is released under the New BSD license (see LICENSE.txt). Go to the project
home page for more info:

http://code.google.com/p/inih/

*/


#include "daemon_ini.h"

#include "logMacros.h"
#include "returnCodes.h"

#define MAX_SECTION 50
#define MAX_NAME 50

/* Strip whitespace chars off end of given string, in place. Return s. */
static char* rstrip(char* s)
{
    char* p = s + strlen(s);
    while (p > s && isspace((unsigned char)(*--p)))
        *p = '\0';
    return s;
}

/* Return pointer to first non-whitespace char in given string. */
static char* lskip(const char* s)
{
    while (*s && isspace((unsigned char)(*s)))
        s++;
    return (char*)s;
}

/* Return pointer to first char c or ';' comment in given string, or pointer to
   null at end of string if neither found. ';' must be prefixed by a whitespace
   character to register as a comment. */
static char* find_char_or_comment(const char* s, char c)
{
    int was_whitespace = 0;
    while (*s && *s != c && !(was_whitespace && *s == ';')) {
        was_whitespace = isspace((unsigned char)(*s));
        s++;
    }
    return (char*)s;
}

/* Version of strncpy that ensures dest (size bytes) is null-terminated. */
static char* strncpy0(char* dest, const char* src, size_t size)
{
    strncpy(dest, src, size-1);
    dest[size - 1] = '\0';
    return dest;
}


int ini_get_config_value ( const char * filename,
                           string       section,
                           string       name,
                           string     & value,
                           bool         maybe_missing )
{
    FILE* file;
    int rc = -1 ;
                
    dlog2 ("config file: %s\n", filename );

    file = fopen(filename, "r");
    if (file)
    {
        char _line[INI_MAX_LINE];
    
        char* _start;
        char* _end;
        char* _name;
        char* _value;

        int _lineno = 0;
        bool in_section = false ;

        /* Scan through file line by line */
        while (fgets(_line, INI_MAX_LINE, file) != NULL)
        {
            _lineno++;
            _start = _line ;
            _start = lskip(rstrip(_start));

            /* skip '#' or ';' comments at start of line */
            if ( ( *_start != ';' ) &&  ( *_start != '#') )
            {
                if (( *_start == '[') && ( *(_start+1) != ']' ))
                {
                    string _section = "[" ;
                    _section.append(section);
                    _section.append("]");

                    string line_string = _line ;
                    if ( line_string.find(_section) != std::string::npos )
                    {
                        dlog3 ("Section: %s (line:%s)\n", _section.c_str(), line_string.c_str());
                        in_section = true ;

                    }
                    else
                    {
                        if ( in_section == true )
                        {
                            in_section = false ;
                        }
                    }
                }
                else if ( in_section == false )
                {
                    ; /* keep looking for specified section */
                }
                /* fields are delimited by a ';' 
                 * field = value              ; */
                else if (*_start && *_start != ';')
                {
                    // dlog ("Line: %s\n", _line );
                    
                    /* Not a comment, must be a name[=:]value pair */
                    _end = find_char_or_comment(_start, '=');
                    if (*_end != '=')
                    {
                        _end = find_char_or_comment(_start, ':');
                    }
                    if (*_end == '=' || *_end == ':')
                    {
                        *_end = '\0';
                        _name = rstrip(_start);

                        // dlog ("Name: %s\n", _name );

                        _value = lskip(_end + 1);
                        _end = find_char_or_comment(_value, '\0');
                        if (*_end == ';')
                        {
                            *_end = '\0';
                        }
                        rstrip(_value);


                        /* if the label match then sae the value and exit */
                        if ( !name.compare(_name) )
                        {
                            value = _value ;
                            dlog2 ("key:value - %s:%s\n", name.c_str(), value.c_str() );
                            fclose(file);
                            return (PASS);
                        }
                    }
                }
            }
        }
        fclose(file);
    }
    if ( maybe_missing == false )
    {
        wlog ("Failed to find label '%s' in section '[%s]'\n",  name.c_str(), section.c_str() );
    }
    else
    {
        rc = PASS ;
    }
    return (rc) ;
}
                    
/* See documentation in header file. */
int ini_parse_file(FILE* file,
                   int (*handler)(void*, const char*, const char*,
                                  const char*),
                   void* user)
{
    /* Uses a fair bit of stack (use heap instead if you need to) */
#if INI_USE_STACK
    char line[INI_MAX_LINE];
#else
    char* line;
#endif
    char section[MAX_SECTION] = "";
    char prev_name[MAX_NAME] = "";

    char* start;
    char* end;
    char* name;
    char* value;
    int lineno = 0;
    int error = 0;

#if !INI_USE_STACK
    line = (char*)malloc(INI_MAX_LINE+1);
    if (!line) {
        return -2;
    }
#endif

    /* Scan through file line by line */
    while (fgets(line, INI_MAX_LINE, file) != NULL) {
        lineno++;

        start = line;
#if INI_ALLOW_BOM
        if (lineno == 1 && (unsigned char)start[0] == 0xEF &&
                           (unsigned char)start[1] == 0xBB &&
                           (unsigned char)start[2] == 0xBF) {
            start += 3;
        }
#endif
        start = lskip(rstrip(start));

        if (*start == ';' || *start == '#') {
            /* Per Python ConfigParser, allow '#' comments at start of line */
        }
#if INI_ALLOW_MULTILINE
        else if (*prev_name && *start && start > line) {
            /* Non-black line with leading whitespace, treat as continuation
               of previous name's value (as per Python ConfigParser). */
            if (!handler(user, section, prev_name, start) && !error)
                error = lineno;
        }
#endif
        else if (*start == '[') {
            /* A "[section]" line */
            end = find_char_or_comment(start + 1, ']');
            if (*end == ']') {
                *end = '\0';
                strncpy0(section, start + 1, sizeof(section));
                *prev_name = '\0';
            }
            else if (!error) {
                /* No ']' found on section line */
                error = lineno;
            }
        }
        else if (*start && *start != ';') {
            /* Not a comment, must be a name[=:]value pair */
            end = find_char_or_comment(start, '=');
            if (*end != '=') {
                end = find_char_or_comment(start, ':');
            }
            if (*end == '=' || *end == ':') {
                *end = '\0';
                name = rstrip(start);
                value = lskip(end + 1);
                end = find_char_or_comment(value, '\0');
                if (*end == ';')
                    *end = '\0';
                rstrip(value);

                /* Valid name[=:]value pair found, call handler */
                strncpy0(prev_name, name, sizeof(prev_name));
                if (!handler(user, section, name, value) && !error)
                    error = lineno;
            }
            else if (!error) {
                /* No '=' or ':' found on name[=:]value line */
                error = lineno;
            }
        }
    }

#if !INI_USE_STACK
    free(line);
#endif

    return error;
}

/* See documentation in header file. */
int ini_parse(const char* filename,
              int (*handler)(void*, const char*, const char*, const char*),
              void* user)
{
    FILE* file;
    int error;

    file = fopen(filename, "r");
    if (!file)
        return -1;
    error = ini_parse_file(file, handler, user);
    fclose(file);
    return error;
}
