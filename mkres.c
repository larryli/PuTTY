/*
 *
 * Commands ...
 *
 * INIT(...)	: Set configs. (rowstep, totwidth, stdheight, stdwidth)
 * SPOS(V,H)	: Set current position.
 *
 * SCOL		: Standard column, for a single column, increment position
 * NEXT(V,H) 	: increment postion by standard + (V,H)
 * COL(K,N)	: X,Y position for col K of N (default 1of [same as last])
 * SS		: Standard size of last COL (using number of cols).
 * ADJ(V,H)	: Adjust next COL or SCOL by (V,H)
 *
 * CURX(H)	: Current Xpos+H
 * CURY(V)	: Current Ypos+V
 *
 */

#include <stdio.h>

FILE * ifd;
FILE * ofd;

char wordbuf[256];

#define T_INIT		100	/* Init the style of the values */
#define T_SPOS		101	/* Set the current position */
#define T_SCOL		102	/* Standard column COL(1,1), SS NEXT */
#define T_NEXT		103	/* Next line */
#define T_COL		104	/* Multi-column */
#define T_SS		105	/* Standard Size */
#define T_ADJ		106	/* Adjust next COL/SCOL */
#define T_GRID		107	/* */
#define T_GAP		108	/* */

#define T_CURX		120	/* Current X + offset */
#define T_CURY		121	/* Current Y + offset */

#define T_SAVEPOSN      122	/* Save current status */
#define T_RESTOREPOSN   123	/* Restore current status */


struct keys {
   char * name;
   int token;
} keywords[] = {
   {"INIT",		T_INIT,		},
   {"SPOS",		T_SPOS,		},
   {"SCOL",		T_SCOL,		},
   {"NEXT",		T_NEXT,		},
   {"COL",		T_COL,		},
   {"SS",		T_SS,		},
   {"ADJ",		T_ADJ,		},
   {"GRID",		T_GRID,		},
   {"GAP",		T_GAP,		},
   {"CURX",		T_CURX,		},
   {"CURY",		T_CURY,		},
   {"SAVEPOSN",		T_SAVEPOSN,	},
   {"RESTOREPOSN",	T_RESTOREPOSN,	},
   {0,0}
};

struct statbuf {
   int token;

   int curx;
   int cury;
   int cols;

   int con_width;
   int con_height;
   int row_step;
   int tot_width;

   int gutter;

   int vadjust;
   int hadjust;
}
   status, saved_status;

int ar_count, ar_val[10];

main(argc, argv)
int argc; 
char ** argv;
{
   int ch;

   ifd = stdin; ofd = stdout;

   while(!feof(ifd))
   {
      if (ferror(ifd))
      {
	 fprintf(stderr, "Error reading input file\n");
	 exit(1);
      }

      if (readword() < 0) break;

      if (check_keys() < 0)
      {
	 fprintf(ofd, "%s", wordbuf);
	 continue;
      }

      /* To get here we have found one of our keywords, some words will 
       * be followed by an argument.
       */

      ar_count = 0;

      while((ch = getc(ifd)) != EOF && isspace(ch) && ch != '\n')
	 putc(ch, ofd);

      if (ch == '(' )
      {
	 for(;;)
	 {
	    ar_val[ar_count++] = get_number();

	    while((ch=getc(ifd)) != EOF && isspace(ch)) ;
	    if (ch != ',') break;
	 }
	 if (ch == EOF) break;
      }
      else
	 ungetc(ch, ifd);

      /* Ok got args, now doit */
      execute_command();
   }
   exit(0);
}

/* This is the lexer - not using lex(1) because this will have to 
 * compile under windows.
 */
int readword()
{
   int ch;
   char *wp;

try_again:;  /* This is for "too big" words and strings. */

   wp=wordbuf;

   /* Find a word ... */
   while((ch=getc(ifd)) != EOF && !isalpha(ch) && ch != '"') 
      putc(ch, ofd);

   if (ch == '"')
   {
      putc(ch, ofd);

      while((ch=getc(ifd)) != EOF && ch != '"') 
         putc(ch, ofd);
      if (ch != EOF)
         putc(ch, ofd);

      goto try_again;
   }

   if (ch == EOF) return -1;

   do
   {
      if (wp>=wordbuf+sizeof(wordbuf)-2)
      {
	 *wp = 0;
	 fprintf(ofd, "%s", wordbuf);

	 while(ch!=EOF && isalpha(ch))
	 {
	    putc(ch, ofd);
	    ch=getc(ifd);
	 }
	 ungetc(ch, ifd);

	 goto try_again;
      }
      *wp++ = ch;
      ch = getc(ifd);
   }
   while(ch != EOF && (isalnum(ch) || ch == '_'));
   *wp = 0;

   ungetc(ch, ifd);
   return wp-wordbuf;
}

int 
get_number()
{
   int ch;
   int sign = 0;
   int value = 0;

   while((ch=getc(ifd)) != EOF && isspace(ch)) ;

   if( ch == '+' )      { sign=1; ch=getc(ifd); }
   else if( ch == '-' ) { sign=-1; ch=getc(ifd); }

   while(ch>='0' && ch<='9')
   {
      value = value * 10 + ch - '0';
      ch = getc(ifd);
   }

   ungetc(ch, ifd);
   if (sign < 0) value = -value;

   return value;
}

check_keys()
{
   struct keys *p;

   for(p=keywords; p->name; p++)
   {
      if (strcmp(wordbuf, p->name) == 0 )
      {
	 status.token = p->token;
	 return p->token;
      }
   }
   return -1;
} 

execute_command()
{
   if (status.cols < 1) status.cols = 1;

   switch(status.token)
   {
   case T_INIT:
      if (ar_count > 0) status.row_step = ar_val[0];
      if (ar_count > 1) status.tot_width = ar_val[1];
      if (ar_count > 2) status.con_height = ar_val[2];
      else              status.con_height = status.row_step;
      if (ar_count > 3) status.con_width = ar_val[3];
      else              status.con_width = status.tot_width;

      status.gutter = ( status.tot_width - status.con_width ) /2;
      break;

   case T_SPOS:
      status.cury = status.curx = 0;
      if (ar_count > 0) status.cury = ar_val[0];
      if (ar_count > 1) status.curx = ar_val[1];
      break;

   case T_SCOL:
      fprintf(ofd, "%d, %d", status.curx + status.hadjust,
	                    status.cury + status.vadjust);
      status.hadjust = status.vadjust = 0;

      fprintf(ofd, ", %d, %d", status.con_width, status.con_height);

      status.cury += status.row_step;

      if (ar_count > 0) status.cury += ar_val[0];
      if (ar_count > 1) status.curx += ar_val[1];
      break;

   case T_NEXT:
      status.cury += status.row_step;
      if (ar_count > 0) status.cury += ar_val[0];
      if (ar_count > 1) status.curx += ar_val[1];
      break;

   case T_COL:
      {
	 int curcol;
	 int col_pos;

	 if (ar_count > 0) curcol = ar_val[0]; else curcol = 1;
	 if (ar_count > 1) status.cols = ar_val[1];

	 col_pos = (status.con_width+status.gutter) *(curcol-1) /status.cols;

	 fprintf(ofd, "%d, %d",
	       status.curx + status.hadjust + col_pos,
	       status.cury + status.vadjust);
	 status.hadjust = status.vadjust = 0;
      }
      break;

   case T_SS:
      {
	 int wm = 1, hm=1;
	 int width;
	 if (ar_count > 0) wm = ar_val[0];
	 if (ar_count > 1) hm = ar_val[1];

	 width = (status.con_width+status.gutter) / status.cols;
	 width *= wm;
	 width -= status.gutter;

	 fprintf(ofd, "%d, %d", width, hm*status.con_height);
      }
      break;

   case T_ADJ:
      if (ar_count > 0) status.vadjust = ar_val[0];
      if (ar_count > 1) status.hadjust = ar_val[0];
      break;

   case T_GRID:
      if (ar_count > 0) status.cols = ar_val[0];
      else              status.cols = 1;
      if (ar_count > 1) status.con_height = ar_val[1];
      if (ar_count > 2) status.row_step   = ar_val[2];
      break;

   case T_GAP:
      if (ar_count > 0) status.cury += ar_val[0];
      else              status.cury += 2;
      break;

   case T_CURX:
      if (ar_count>0)
	 fprintf(ofd, "%d", status.curx+ar_val[0]);
      else
	 fprintf(ofd, "%d", status.curx);
      break;
   case T_CURY:
      if (ar_count>0)
	 fprintf(ofd, "%d", status.cury+ar_val[0]);
      else
	 fprintf(ofd, "%d", status.cury);
      break;

   case T_SAVEPOSN:
      saved_status = status;
      break;
   case T_RESTOREPOSN:
      status = saved_status;
      break;
   }
}
