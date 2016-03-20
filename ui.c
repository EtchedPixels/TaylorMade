/*
 *	Simple curses UI implementation. You probably want to replace
 *	this with something more useful
 */
 
#include <curses.h>
#include <stdlib.h>
#include <ctype.h>

#include "taylormade.h"

static WINDOW *Top;
static WINDOW *Bottom;
static int OutputPos;
static int OutLine;
static int OutC;
static char OutWord[128];
static int Window;
static int SavedPos;

static void Scroll(WINDOW *w)
{
	scroll(w);
	wmove(w, 11, 0);
	OutputPos = 0;
}

static void WriteChar(WINDOW *w, char c)
{
	wmove(w, OutLine, OutputPos);
	wprintw(w, "%c", c);
	OutputPos += 1;
}

static void WordFlush(WINDOW *w)
{
	int i;
	for(i = 0; i < OutC; i++)
		WriteChar(w, OutWord[i]);
	OutC = 0;
}

void TopWindow(void)
{
	WordFlush(Bottom);
	SavedPos = OutputPos;
	OutLine = 0;
	OutputPos = 0;
	Window = 0;
	wmove(Top, 0, 0);
	wclear(Top);
}

void BottomWindow(void)
{
	WordFlush(Top);
	OutputPos = SavedPos;
	OutLine = 11;
	Window = 1;
	wrefresh(Top);
	wmove(Bottom, 11, OutputPos);
}

void PrintCharacter(unsigned char c)
{
	if(OutC == 0 &&  c ==' ')
		return;
	if(Window == 1) {
		if(isspace(c)) {
			WordFlush(Bottom);
			WriteChar(Bottom, ' ');
			if(c == '\n')
				Scroll(Bottom);
			return;
		}
		OutWord[OutC] = c;
		OutC++;
		if(OutC > 79)
			WordFlush(Bottom);
		else if(OutC + OutputPos > 78)
			Scroll(Bottom);
		return;
	} else {
		if(isspace(c)) {
			WordFlush(Top);
			WriteChar(Top, ' ');
			if(c == '\n') {
				OutLine++;
				OutputPos = 0;
				wmove(Top, OutLine, OutputPos);
			}
			return;
		}
		OutWord[OutC] = c;
		OutC++;
		if(OutC == 78)
			WordFlush(Top);
		else if(OutC + OutputPos > 78) {
			OutLine++;
			OutputPos = 0;
		}
		return;
	}
}

unsigned char WaitCharacter(void)
{
	WordFlush(Bottom);
	wrefresh(Bottom);
	return wgetch(Bottom);
}

void LineInput(char *buf, int len)
{
	int pos=0;
	int ch;
	while(1)
	{
		wrefresh(Bottom);
		ch=wgetch(Bottom);
		switch(ch)
		{
			case 10:;
			case 13:;
				buf[pos]=0;
				scroll(Bottom);
				wmove(Bottom, 11, 0);
				OutputPos = 0;
				return;
			case 8:;
			case 127:;
				if(pos>0)
				{
					int y,x;
					getyx(Bottom,y,x);
					x--;
					if(x==-1)
					{
						x=79;
						y--;
					}
					mvwaddch(Bottom,y,x,' ');
					wmove(Bottom,y,x);
					wrefresh(Bottom);
					pos--;
				}
				break;
			default:
				if(pos >= len)
					break;
				if(ch>=' '&&ch<=126)
				{
					buf[pos++]=ch;
					waddch(Bottom,(char)ch);
					wrefresh(Bottom);
				}
				break;
		}
	}
}

static void DisplayEnd(void) 
{
	endwin();
}

void DisplayInit(void)
{
	initscr();
	atexit(DisplayEnd);
	Top = newwin(12, 80, 0 , 0);
	Bottom = newwin(12, 80, 12, 0);
	scrollok(Bottom, TRUE);
	scrollok(Top, FALSE);
	leaveok(Top, TRUE);
	leaveok(Bottom, FALSE);
	idlok(Bottom, TRUE);
	noecho();
	cbreak();

}
