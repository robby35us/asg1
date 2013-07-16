/* lexanc.c */
/* modified by Robert E Reed

/* Copyright (c) 2001 Gordon S. Novak Jr. and
   The University of Texas at Austin. */

/*
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "token.h"
#include "lexan.h"


TOKEN createOperator(int opVal, TOKEN tok)
{
    tok->tokentype = OPERATOR;
    tok->whichval = opVal;
    return (tok);
}

TOKEN createDelimiter(int delimVal, TOKEN tok)
{
    tok->tokentype = DELIMITER;
    tok->whichval = delimVal - DELIMITER_BIAS;
    return (tok);
}

/* Skip blanks, whitespace and comments. */
void skipblanks ()
{
    int c;
    int cc;
    while ((c = peekchar()) != EOF
             && (c == ' ' || c == '\n' || c == '\t' || c == '{' 
             || (c == '(' && (cc = peek2char()) == '*')))
    {
        if(c == '{')
        {
            do
            {
                getchar();
            }while ((c = peekchar()) != EOF && c != '}');
        }
        else if(c == '(')
        {
            do
            {
                getchar();
                c = cc;
                cc = peek2char();
            }while (cc != EOF 
                       && (c != '*' || cc != ')'));
            getchar();
            c = cc;
        }

        // move pointer to end of current comment or whitespace 
        if(c != EOF)
            getchar();
    }
}

/* Get identifiers and reserved words */
TOKEN identifier (TOKEN tok)
  {
    }

TOKEN getstring (TOKEN tok)
  {
    }

TOKEN special (TOKEN tok)
{
    char c = peekchar();
    char cc = peek2char();
    switch(c)
		{
			case '+': tok = createOperator(PLUSOP, tok);
			    break;
			case '-': tok = createOperator(MINUSOP, tok);
				  break;
			case '*': tok = createOperator(TIMESOP, tok);
			    break;
			case '/': tok = createOperator(DIVIDEOP, tok);
			    break;
			case ':': if(cc == '=') {
					         tok = createOperator(ASSIGNOP, tok);
							     getchar();
					      }else
						       tok = createDelimiter(COLON, tok);
					      break;
			case '=': tok = createOperator(EQOP, tok);
			    break;
			case '<': if(cc == '>'){
				           tok = createOperator(NEOP, tok);
									 getchar();
								}else if(cc == '='){
									 tok = createOperator(LEOP, tok);
									 getchar();
								}else
									 tok = createOperator(LTOP, tok);
								break;
			case '>': if(cc == '='){
				           tok = createOperator(GEOP, tok);
								   getchar();
								}else
									 tok = createOperator(GTOP, tok);
								break;
			case '^': tok = createOperator(POINTEROP, tok);
		      break;
			case '.': if(cc == '.'){
				           tok = createDelimiter(DOTDOT, tok);
									 getchar();
								}else
				           tok = createOperator(DOTOP, tok);
								break;
			case ',': tok = createDelimiter(COMMA, tok);
			    break;
			case ';': tok = createDelimiter(SEMICOLON, tok);
			    break;
			case '(': tok = createDelimiter(LPAREN, tok);
			    break;
			case ')': tok = createDelimiter(RPAREN, tok);
			    break;
			case '[': tok = createDelimiter(LBRACKET, tok);
			    break;
			case ']': tok = createDelimiter(RBRACKET, tok);
			    break;
		}
    getchar();
		return tok;
}

/* Get and convert unsigned numbers of all types. */
TOKEN number (TOKEN tok)
  { long num;
    int  c, charval;
    num = 0;
    while ( (c = peekchar()) != EOF
            && CHARCLASS[c] == NUMERIC)
      {   c = getchar();
          charval = (c - '0');
          num = num * 10 + charval;
        }
    tok->tokentype = NUMBERTOK;
    tok->datatype = INTEGER;
    tok->intval = num;
    return (tok);
  }

