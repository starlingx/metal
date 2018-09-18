/*
 * Copyright (c) 2015-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */
 
 /**
  * @file
  * Wind River - Titanium Cloud - Regex Utilty Module
  */

#include <iostream>
#include <string>
#include <regex>

using namespace std;

#include "nodeBase.h"
#include "regexUtil.h"

/**********************************************************************
 *
 * Name       : regexUtil_label_match
 *
 * Purpose    : Verify that the rule matches the pattern
 *
 * Description: Loop over the rule extracting the labels that are
 *              delimited by ':'s. Then for each label, walk the pattern
 *              verifying that the label exists while maintaining a
 *              forward position search. If all the rule labels are 
 *              found (in order) in the pattern then return a true.
 *              Otherwise, return false.
 *
 **********************************************************************/

bool regexUtil_label_match ( string hostname, string pattern, string rule )
{
    bool match = false ;

    if ( rule.empty() )
    {
        wlog ("%s empty rule string\n", hostname.c_str() );
    }
    else
    {
        int i ;
        size_t pos = 0 ;
        size_t new_pos = 0 ;
        size_t run_pos = 0 ;
        int colins = 0 ;
        int len = (int)rule.length() ;
        char * ptr = (char*)rule.data();
        for ( int i = 0 ; i < len ; i++ )
        {
            if ( *ptr++ == ':' ) colins++ ;
        }
        dlog ("%s there are %d colins in the rule (%s)\n", hostname.c_str() , colins, rule.c_str());
        match = true ;
        i = 0 ;
        do
        {
            string label ;
            if ( colins )
            {
                /* get the label from the current position up to the next colin */
                new_pos = rule.find( ':' , pos );
                if ( new_pos != std::string::npos )
                {
                    label = rule.substr(pos, new_pos-pos) ;
                    dlog2 ("%s %d-%s (%ld-%ld)\n", hostname.c_str() , i, label.c_str(), pos, new_pos);
                    
                    /* move beyond the ':' */
                    pos = ++new_pos ;
                }
            }
            else
            {
                label = rule.substr(pos) ;
                dlog ("%s %d:%s\n", hostname.c_str(), i, label.c_str());
            }
            
            if ( label.empty() )
            {
                wlog ("%s label not found\n", hostname.c_str());
                match = false ;
            }
            else
            {
                run_pos = pattern.find( label, run_pos ) ;
                if ( run_pos != std::string::npos )
                {
                   dlog1 ("%s '%s' found in pattern (pos:%ld)\n", hostname.c_str(), label.c_str(),run_pos);
                   run_pos++ ;
                }
                else
                {
                    dlog3 ("%s '%s' NOT found in pattern \n", 
                              hostname.c_str(),
                              label.c_str());
                    match = false ;
                }
            }
            i++ ;
        } while ( ( colins--) && ( match == true ) ) ;
    }
    return (match);
}

bool regexUtil_pattern_match ( std::string pattern , std::string rule, int type )
{
    bool result = false ;
    if ( type == 1 )
    {
       //std::cmatch cm;    // same as std::match_results<const char*> cm;
       //std::regex e (rule.data()) ;
       //result = std::regex_match (pattern,cm, e , std::regex_constants::match_not_bol );
       if ( result )
       {
            std::cout << "Flagged String match\n";
       }
    }
    else
    {
        result = std::regex_match (pattern, std::regex(rule)) ;
        if ( result )
        {
            std::cout << "String literal matched\n";
        
 
            std::smatch sm;    // same as std::match_results<string::const_iterator> sm;
            std::regex_match (pattern,sm,std::regex(rule));
       
            std::cout << "String object with " << sm.size() << " matches\n";
 
            std::cout << "The matches are: ";
            for (unsigned i=0; i<sm.size(); ++i) 
            {
                std::cout << "[" << sm[i] << "] ";
            }
        }
    }
    if ( result == false )
    {
         std::cout << "No Match\n" ;
    }
    printf ("\n");
    return (result);
}

/**********************************************************************
 *
 * Name       : regexUtil_string_startswith
 *
 * Purpose    : Verify that the substring is at the beginning of a string
 *
 * Description: original string starts with substring
 *
 **********************************************************************/
bool regexUtil_string_startswith ( const char *original, const char *substring )
{
    if (strncmp(original, substring, strlen(substring)) == 0)
        return true;
    return false;
}



#ifdef XXXX
int main ()
{
    string pattern = "subject"    ;
    string rule    = "(subj)(.*)" ;

    return ( regexUtil_pattern_match ( pattern, rule ));
}
#endif

#ifdef WANT_MAIN

int main ()
{

  if (std::regex_match ("subject", std::regex("(subj)(.*)") ))
    std::cout << "string literal matched\n";

  const char cstr[] = "subject";
  std::string s ("subject");
  std::regex e ("(subj)(.*)");

  if (std::regex_match (s,e))
    std::cout << "string object matched\n";

  if ( std::regex_match ( s.begin(), s.end(), e ) )
    std::cout << "range matched\n";

  std::cmatch cm;    // same as std::match_results<const char*> cm;
  std::regex_match (cstr,cm,e);
  std::cout << "string literal with " << cm.size() << " matches\n";

  std::smatch sm;    // same as std::match_results<string::const_iterator> sm;
  std::regex_match (s,sm,e);
  std::cout << "string object with " << sm.size() << " matches\n";

  std::regex_match ( s.cbegin(), s.cend(), sm, e);
  std::cout << "range with " << sm.size() << " matches\n";

  // using explicit flags:
  std::regex_match ( cstr, cm, e, std::regex_constants::match_default );

  std::cout << "the matches were: ";
  for (unsigned i=0; i<sm.size(); ++i) {
    std::cout << "[" << sm[i] << "] ";
  }

  std::cout << std::endl;

  return 0;
}
#endif
