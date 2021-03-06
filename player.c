#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#include "taylormade.h"

static unsigned char Flag[128];
static unsigned char Object[256];
static unsigned char Word[5];

/* OOPS buffer for later games */
static unsigned char OopsFlag[128];
static unsigned char OopsObject[256];
/* RAM save buffer for later games */
static unsigned char RamFlag[128];
static unsigned char RamObject[256];

static unsigned char Image[131072];
static int ImageLen;
static int VerbBase;
static unsigned int TokenBase;
static unsigned int MessageBase;
static unsigned int Message2Base;
static unsigned int RoomBase;
static unsigned int ObjectBase;
static unsigned int ExitBase;
static unsigned int ObjLocBase;
static unsigned int StatusBase;
static unsigned int ActionBase;
static unsigned int FlagBase;

static int NumLowObjects;

static int ActionsDone;
static int ActionsExecuted;
static int Redraw;

static int GameVersion;
static int Blizzard;

#ifdef DEBUG

/*
 *	Debugging
 */
static unsigned char WordMap[256][5];

static char *Condition[]={
	"<ERROR>",
	"AT",
	"NOTAT",
	"ATGT",
	"ATLT",
	"PRESENT",
	"HERE",
	"ABSENT",
	"NOTHERE",
	"CARRIED",
	"NOTCARRIED",
	"WORN",
	"NOTWORN",
	"NODESTROYED",
	"DESTROYED",
	"ZERO",
	"NOTZERO",
	"WORD1",
	"WORD2",
	"WORD3",
	"CHANCE",
	"LT",
	"GT",
	"EQ",
	"NE",
	"OBJECTAT",
	"COND26",
	"COND27",
	"COND28",
	"COND29",
	"COND30",
	"COND31",
};

static char *Action[]={
	"<ERROR>",
	"LOAD?",
	"QUIT",
	"INVENTORY",
	"ANYKEY",
	"SAVE",
	"DROPALL",
	"LOOK",
	"OK",		/* Guess */
	"GET",
	"DROP",
	"GOTO",
	"GOBY",
	"SET",
	"CLEAR",
	"MESSAGE",
	"CREATE",
	"DESTROY",
	"PRINT",
	"DELAY",
	"WEAR",
	"REMOVE",
	"LET",
	"ADD",
	"SUB",
	"PUT",		/* ?? */
	"SWAP",
	"SWAPF",
	"MEANS",
	"PUTWITH",
	"BEEP",		/* Rebel Planet at least */
	"REFRESH?",
	"RAMSAVE",
	"RAMLOAD",
	"ACT34",
	"OOPS",
	"ACT36",
	"ACT37",
	"ACT38",
	"ACT39",
	"ACT40",
	"ACT41",
	"ACT42",
	"ACT43",
	"ACT44",
	"ACT45",
	"ACT46",
	"ACT47",
	"ACT48",
	"ACT49",
	"ACT50",
};

static void LoadWordTable(void)
{
	unsigned char *p = Image + VerbBase;

	while(1) {
		if(p[4] == 255)
			break;
		if(WordMap[p[4]][0] == 0)
			memcpy(WordMap[p[4]], p, 4);
		p+=5;
	}
}

static void PrintWord(unsigned char word)
{
	if(word == 126)
		fprintf(stderr, "*    ");
	else if(word == 0 || WordMap[word][0] == 0)
		fprintf(stderr, "%-4d ", word);
	else {
		fprintf(stderr, "%c%c%c%c ",
			WordMap[word][0],
			WordMap[word][1],
			WordMap[word][2],
			WordMap[word][3]);
	}
}

#endif

static int FindCode(char *x, int base)
{
	unsigned char *p = Image + base;
	int len = strlen(x);
	while(p < Image + ImageLen - len) {
		if(memcmp(p, x, len) == 0)
			return p - Image;
		p++;
	}
	return -1;
}

static int FindFlags(void) 
{
	/* Look for the flag initial block copy */
	int pos = FindCode("\x01\x06\x00\xED\xB0\xC9\x00\xFD", 0);
	if(pos == -1) {
		fprintf(stderr, "Cannot find initial flag data.\n");
		exit(1);
	}
	return pos + 6;
}

static int FindObjectLocations(void)
{
	int pos = FindCode("\x01\x06\x00\xED\xB0\xC9\x00\xFD", 0);
	if(pos == -1) {
		fprintf(stderr, "Cannot find initial object data.\n");
		exit(1);
	}
	pos = Image[pos - 16] + (Image[pos - 15] << 8);
	return pos - 0x4000;
}

static int FindExits(void) 
{
	int pos = 0;
	
	while((pos = FindCode("\x1A\xBE\x28\x0B\x13", pos+1)) != -1)
	{
		pos = Image[pos - 5] + (Image[pos - 4] << 8);
		pos -= 0x4000;
		return pos;
	}
	fprintf(stderr, "Cannot find initial flag data.\n");
	exit(1);
}

static int LooksLikeTokens(int pos)
{
	unsigned char *p = Image + pos;
	int n = 0;
	int t = 0;
	while(n < 512) {
		unsigned char c = p[n] & 0x7F;
		if(c >= 'a' && c <= 'z')
			t++;
		n++;
	}
	if(t > 300)
		return 1;
	return 0;
}

static void TokenClassify(int pos)
{
	unsigned char *p = Image + pos;
	int n = 0;
	while(n++ < 256) {
		do {
			if(*p == 0x5E || *p == 0x7E)
				GameVersion = 0;
		} while(!(*p++ & 0x80));
	}
}

static int FindTokens(void)
{
	int addr;
	int pos = 0;
	do {
		pos = FindCode("\x47\xB7\x28\x0B\x2B\x23\xCB\x7E", pos + 1);
		if(pos == -1) {
			/* Last resort */
			addr = FindCode("You are in ", 0) - 1;
			if(addr == -1) {
				fprintf(stderr, "Unable to find token table.\n");
				exit(1);
			}
			return addr;
		}
		addr = (Image[pos-1] <<8 | Image[pos-2]) - 0x4000;
	}
	while(LooksLikeTokens(addr) == 0);
	TokenClassify(addr);
	return addr;
}

static char LastChar = 0;
static int Upper = 0;
static int PendSpace = 0;

static void OutWrite(char c)
{
	if(isalpha(c) && Upper)
	{
		c = toupper(c);
		Upper = 0;
	}
	PrintCharacter(c);
}

static void OutFlush(void)
{
	if(LastChar)
		OutWrite(LastChar);
	if(PendSpace)
		OutWrite(' ');
	LastChar = 0;
}

static void OutReset(void)
{
	OutFlush();
}

static void OutCaps(void)
{
	if (LastChar) {
		OutWrite(LastChar);
		LastChar = 0;
	}
	Upper = 1;
}

static void OutChar(char c)
{
	if(c == ']')
		c = '\n';
	if(c == ' ') {
		PendSpace = 1;
		return;
	}
	if(LastChar) {
		OutWrite(LastChar);
		LastChar = 0;
	}
	if(PendSpace) {
		OutWrite(' ');
		PendSpace = 0;
	}
	if(c == '.')
		Upper = 1;
	LastChar = c;
}

static void OutReplace(char c)
{
	LastChar = c;
}

static void OutKillSpace() 
{
	PendSpace = 0;
}

static void OutString(char *p)
{
	while(*p)
		OutChar(*p++);
}

static unsigned char *TokenText(unsigned char n)
{
	unsigned char *p = Image + TokenBase;
			
	p = Image + TokenBase;
	
	while(n > 0) {
		while((*p & 0x80) == 0)
			p++;
		n--;
		p++;
	}
	return p;
}

static void PrintToken(unsigned char n)
{
	unsigned char *p = TokenText(n);
	unsigned char c;
	do {
		c = *p++;
		OutChar(c & 0x7F);
	} while(!(c & 0x80));
}

static void PrintText1(unsigned char *p, int n)
{
	while(n > 0) {
		while(*p != 0x7E && *p != 0x5E)
			p++;
		n--;
		p++;
	}
	while(*p != 0x7E && *p != 0x5E)
		PrintToken(*p++);
	if(*p == 0x5E)
		PendSpace = 1;
}

/*
 *	Version 0 is different
 */
 
static void PrintText0(unsigned char *p, int n)
{
	unsigned char *t = NULL;
	unsigned char c;
	while(1) {
		if(t == NULL)
			t = TokenText(*p++);
		c = *t & 0x7F;
		if(c == 0x5E || c == 0x7E) {
			if(n == 0) {
				if(c == 0x5E)
					PendSpace = 1;
				return;
			}
			n--;
		}
		else if(n == 0)
			OutChar(c);
		if(*t++ & 0x80)
			t = NULL;
	}
}

static void PrintText(unsigned char *p, int n)
{
	if(GameVersion == 0) 	/* In stream end markers */
		PrintText0(p, n);
	else			/* Out of stream end markers (faster) */
		PrintText1(p, n);
}

static int FindMessages(void)
{
	int pos = 0;
	/* Newer game format */
	while((pos = FindCode("\xF5\xE5\xC5\xD5\x3E\x2E", pos+1)) != -1) {
		if(Image[pos + 6] != 0x32)
			continue;
		if(Image[pos + 9] != 0x78)
			continue;
		if(Image[pos + 10] != 0x32)
			continue;
		if(Image[pos + 13] != 0x21)
			continue;
		return (Image[pos+14] + (Image[pos+15] << 8)) - 0x4000;
	}
	/* Try now for older game format */
	while((pos = FindCode("\xF5\xE5\xC5\xD5\x78\x32", pos+1)) != -1) {
		if(Image[pos + 8] != 0x21)
			continue;
		if(Image[pos + 11] != 0xCD)
			continue;
		/* End markers in compressed blocks */
		GameVersion = 0;
		return (Image[pos+9] + (Image[pos+10] << 8)) - 0x4000;
	}
	fprintf(stderr, "Unable to locate messages.\n");
	exit(1);
}

static int FindMessages2(void)
{
	int pos = 0;
	while((pos = FindCode("\xF5\xE5\xC5\xD5\x78\x32", pos+1)) != -1) {
		if(Image[pos + 8] != 0x21)
			continue;
		if(Image[pos + 11] != 0xC3)
			continue;
		return (Image[pos+9] + (Image[pos+10] << 8)) - 0x4000;
	}
	fprintf(stderr, "No second message block ?\n");
	return 0;
}

static void Message(unsigned char m)
{
	unsigned char *p = Image + MessageBase;
	PrintText(p, m);
}

static void Message2(unsigned int m)
{
	unsigned char *p = Image + Message2Base;
	PrintText(p, m);
}

static int FindObjects(void)
{
	int pos = 0;
	while((pos = FindCode("\xF5\xE5\xC5\xD5\x32", pos+1)) != -1) {
		if(Image[pos + 10] != 0xCD)
			continue;
		if(Image[pos +7] != 0x21)
			continue;
		return (Image[pos+8] + (Image[pos+9] << 8)) - 0x4000;
	}
	fprintf(stderr, "Unable to locate objects.\n");
	exit(1);
}

static void PrintObject(unsigned char obj)
{
	unsigned char *p = Image + ObjectBase;
	PrintText(p, obj);
}

/* Standard format */
static int FindRooms(void)
{
	int pos = 0;
	while((pos = FindCode("\x3E\x19\xCD", pos+1)) != -1) {
		if(Image[pos + 5] != 0xC3)
			continue;
		if(Image[pos + 8] != 0x21)
			continue;
		return (Image[pos+9] + (Image[pos+10] << 8)) - 0x4000;
	}
	fprintf(stderr, "Unable to locate rooms.\n");
	exit(1);
}


static void PrintRoom(unsigned char room)
{
	unsigned char *p = Image + RoomBase;
	if (Blizzard && room < 102)
		p = Image + 0x18000;
	PrintText(p, room);
}


static void PrintNumber(unsigned char n)
{
	char buf[4];
	char *p = buf;
	snprintf(buf, 3, "%d", (int)n);
	while(*p)
		OutChar(*p++);
}


static unsigned char Destroyed()
{
	return 252;
}

static unsigned char Carried()
{
	return Flag[2];
}

static unsigned char Worn()
{
	return Flag[3];
}

static unsigned char Location()
{
	return Flag[0];
}

static unsigned char NumObjects()
{
	return Flag[6];
}

static int CarryItem(void)
{
	if(Flag[5] == Flag[4])
		return 0;
	if(Flag[5] < 255)
		Flag[5]++;
	return 1;
}

static void DropItem(void)
{
	if(Flag[5] > 0)
		Flag[5]--;
}

static void Put(unsigned char obj, unsigned char loc)
{
	/* Will need refresh logics somewhere, maybe here ? */
	if(Object[obj] == Location() || loc == Location())
		Redraw = 1;
	Object[obj] = loc;
}

static int Present(unsigned char obj)
{
	unsigned char v = Object[obj];
	if(v == Location() || v == Worn() || v == Carried())
		return 1;
	return 0;
}

static int Chance(int n)
{
	unsigned int v = (rand() >> 12) ^ time(NULL);
	v%=100;
	if(v > n)
		return 0;
	return 1;
}

static void NewGame(void)
{
	Redraw = 1;
	memset(Flag, 0, 128);
	memcpy(Flag + 1, Image + FlagBase, 6);
	memcpy(Object, Image + ObjLocBase, NumObjects());
}

void RamLoad(void)
{
	memcpy(Flag, RamFlag, 128);
	memcpy(Object, RamObject, 256);
	Message(19);
}

static void RamSave(int game)
{
	memcpy(RamFlag,  Flag, 128);
	memcpy(RamObject, Object, 256);
	if(game)
		Message(19);
}

static void Oops(void)
{
	memcpy(Flag, OopsFlag, 128);
	memcpy(Object, OopsObject, 256);
}

static void Checkpoint(void)
{
	memcpy(OopsFlag,  Flag, 128);
	memcpy(OopsObject, Object, 256);
}


static void LoadGame(void)
{
	char c;
	char name[32];
	FILE *f;
	OutCaps();
	Message(26);
	OutFlush();
	
	do {
		c = WaitCharacter();
		if(c == 'n' || c == 'N') {
			OutChar('N');
			OutChar('\n');
			return;
		}
		if(c == 'y' || c == 'Y') {
			OutChar('Y');
			OutChar('\n');
		
			OutString("File name: ");
			LineInput(name, 32);	
			
			f = fopen(name, "r");
			if(f == NULL)
				OutString("Unable to open file.\n");
			else if(fread(Flag, 128, 1, f) != 1 ||
			   fread(Object, 256, 1, f) != 1) {
				OutString("Unable to load game.\n");
				NewGame();
			}
			fclose(f);
			Redraw = 1;
			return;
		}
	}
	while(1);
}

static void QuitGame(void)
{
	char c;
	OutCaps();
	Message(18);
	OutChar(' ');
	OutFlush();
	do {
		c = WaitCharacter();
		if(c == 'n' || c == 'N') {
			OutChar('N');
			OutChar('\n');
			exit(0);
		}
		if(c == 'y' || c == 'Y') {
			OutChar('Y');
			OutChar('\n');
			NewGame();
			return;
		}
	}
	while(1);
}

static void Inventory(void)
{
	int i;
	int f = 0;
	OutCaps();
	Message(16);	/* ".. are carrying: " */
	for(i = 0; i < NumObjects(); i++) {
		if(Object[i] == Carried() || Object[i] == Worn()) {
			f = 1;
			PrintObject(i);
			if(Object[i] == Worn())
				Message(30);
		}
	}
	if(f == 0)
		Message(17); /* "nothing at all" */
	else {
		if(GameVersion == 0) {
			OutKillSpace();
			OutChar('.');
		} else {
			OutReplace('.');
		}
	}
}

static void  AnyKey(void) {
	Message(20);
	OutFlush();
	WaitCharacter();
}

static void SaveGame(void) {
	FILE *f;
	char name[33];
	OutString("File name: ");
	LineInput(name, 32);
	f = fopen(name, "w");
	if(f == NULL) {
		OutString("Save failed.\n");
		return;
	}
	if(fwrite(Flag, 128, 1, f) != 1 || fwrite(Object, 256,1 , f) != 1)
		OutString("Save failed.\n");
	fclose(f);	
}

static void DropAll(void) {
	int i;
	for(i = 0; i < NumObjects(); i++) {
		if(Object[i] == Carried() || Object[i] == Worn())
			Put(i, Location());
	}
	Flag[5] = 0;
}

static void GetObject(unsigned char obj) {
	if(Object[obj] == Carried() || Object[obj] == Worn()) {
		Message(21);
		return;
	}
	if(Object[obj] != Location()) {
		Message(22);
		return;
	}
	if(CarryItem() == 0) {
		Message(15);
		return;
	}
	Put(obj, Carried());
}

static void DropObject(unsigned char obj) {
	/* FIXME: check if this is how the real game behaves */
	if(Object[obj] == Worn()) {
		Message(29);
		return;
	}
	if(Object[obj] != Carried()) {
		Message(23);
		return;
	}
	DropItem();
	Put(obj, Location());
}

static void Look(void) {
	int i; 
	int f = 0;
	unsigned char locw = 0x80|Location();
	unsigned char *p;

	Redraw = 0;
	OutReset();
	OutCaps();
	TopWindow();
	
	if(Flag[1]) {
		Message(25);
		BottomWindow();
		return;
	}
	PrintRoom(Location());
	OutChar(' ');
	for(i = 0; i < NumLowObjects; i++) {
		if(Object[i] == Location())
			PrintObject(i);
	}
	
	p = Image + ExitBase;
	
	while(*p != locw)
		p++;
	p++;
	while(*p < 0x80) {
		if(f == 0)
			Message(13);
		f = 1;
		OutCaps();
		Message(*p);
		p += 2;
	}
	if(f == 1)
	{
		OutReplace('.');
		OutChar('\n');
	}
	f = 0;
	
	for(; i < NumObjects(); i++) {
		if(Object[i] == Location()) {
			if(f == 0) {
				Message(0);
				if( GameVersion == 0)
					OutReplace(0);
			}
			f = 1;
			PrintObject(i);
		}
	}
	if(f == 1)
		OutReplace('.');
	OutChar('\n');
	BottomWindow();
}
	
	
static void Goto(unsigned char loc) {
	Flag[0] = loc;
	Redraw = 1;
}

static void Delay(unsigned char n) {
	sleep(n);
}

static void Wear(unsigned char obj) {
	if(Object[obj] == Worn()) {
		Message(29);
		return;
	}
	if(Object[obj] != Carried()) {
		Message(23);
		return;
	}
	DropItem();
	Put(obj, Worn());
}

static void Remove(unsigned char obj) {
	if(Object[obj] != Worn()) {
		Message(28);
		return;
	}
	if(CarryItem() == 0) {
		Message(15);
		return;
	}
	Put(obj, Carried());
}

static void Means(unsigned char vb, unsigned char no) {
	Word[0] = vb;
	Word[1] = no;
}
		
static void ExecuteLineCode(unsigned char *p)
{
	unsigned char arg1, arg2;
	int n;
	do {
		unsigned char op = *p;
		
		if(op & 0x80)
			break;
		p++;
		arg1 = *p++;
#ifdef DEBUG
		fprintf(stderr, "%s %d ", Condition[op], arg1);
#endif
		if(op > 20)
		{
			arg2 = *p++;
#ifdef DEBUG
			fprintf(stderr, "%d ", arg2);
#endif
		}
		switch(op) {
			case 1:
				if(Location() == arg1)
					continue;
				break;
				
			case 2:
				if(Location() != arg1)
					continue;
				break;
			case 3:
				if(Location() > arg1)
					continue;
				break;
			case 4:
				if(Location() < arg1)
					continue;
				break;
			case 5:
				if(Present(arg1))
					continue;
				break;
			case 6:
				if(Object[arg1] == Location())
					continue;
				break;
			case 7:
				if(!Present(arg1))
					continue;
				break;
			case 8:
				if(Object[arg1] != Location())
					continue;
				break;
			case 9:
				/*FIXME : or worn ?? */
				if(Object[arg1] == Carried())
					continue;
				if(Object[arg1] == Worn())
					continue;
				break;
			case 10:
				/*FIXME : or worn ?? */
				if(Object[arg1] != Carried())
					continue;
				if(Object[arg1] != Worn())
					continue;
				break;
			case 11:
				if(Object[arg1] == Worn())
					continue;
				break;
			case 12:
				if(Object[arg1] != Worn())
					continue;
				break;
			case 13:
				if(Object[arg1] != Destroyed())
					continue;
				break;
			case 14:
				if(Object[arg1] == Destroyed())
					continue;
				break;
			case 15:
				if(Flag[arg1] == 0)
					continue;
				break;
			case 16:
				if(Flag[arg1] != 0)
					continue;
				break;
			case 17:
				if(Word[2] == arg1)
					continue;
				break;
			case 18:
				if(Word[3] == arg1)
					continue;
				break;
			case 19:
				if(Word[4] == arg1)
					continue;
				break;
			case 20:
				if(Chance(arg1))
					continue;
				break;
			case 21:
				if(Flag[arg1] < arg2)
					continue;
				break;
			case 22:
				if(Flag[arg1] > arg2)
					continue;
				break;
			case 23:
				if(Flag[arg1] == arg2)
					continue;
				break;
			case 24:
				if(Flag[arg1] != arg2)
					continue;
				break;
			case 25:
				if(Object[arg1] == arg2)
					continue;
				break;
			default:
				fprintf(stderr, "Unknown condition %d.\n",
					op);
				break;
		}
#ifdef DEBUG
		fprintf(stderr, "\n");
#endif
		return;
	} while(1);
	
	ActionsExecuted = 1;
	
	do {
		unsigned char op = *p;
		if(!(op & 0x80))
			break;

#ifdef DEBUG
		if(op & 0x40)
			fprintf(stderr, "DONE:");
		fprintf(stderr,"%s(%d) ", Action[op & 0x3F], op & 0x3F);
#endif
		
		p++;
		if(op & 0x40)
			ActionsDone = 1;
		op &= 0x3F;

		
		if(op > 8) {
			arg1 = *p++;
#ifdef DEBUG
			fprintf(stderr, "%d ", arg1);
#endif
		}
		if(op > 21) {
			arg2 = *p++;
#ifdef DEBUG
			fprintf(stderr, "%d ", arg2);
#endif
		}
		switch(op) {
			case 1:
				LoadGame();
				break;
			case 2:
				QuitGame();
				break;
			case 3:
				Inventory();
				break;
			case 4:
				AnyKey();
				break;
			case 5:
				SaveGame();
				break;
			case 6:
				DropAll();
				break;
			case 7:
				Look();
				break;
			case 8:
				/* Guess */
				Message(8);
				break;
			case 9:
				GetObject(arg1);
				break;
			case 10:
				DropObject(arg1);
				break;
			case 11:
				Goto(arg1);
				break;
			case 12:
				/* Blizzard pass era */
				if(GameVersion == 1)
					Goto(Object[arg1]);
				else
					Message2(arg1);
				break;
			case 13:
				Flag[arg1] = 255;
				break;
			case 14:
				Flag[arg1] = 0;
				break;
			case 15:
				Message(arg1);
				break;
			case 16:
				Put(arg1, Location());
				break;
			case 17:
				Put(arg1, Destroyed());
				break;
			case 18:
				PrintNumber(Flag[arg1]);
				break;
			case 19:
				Delay(arg1);
				break;
			case 20:
				Wear(arg1);
				break;
			case 21:
				Remove(arg1);
				break;
			case 22:
				Flag[arg1] = arg2;
				break;
			case 23:
				n = Flag[arg1] + arg2;
				if(n > 255)
					n = 255;
				Flag[arg1] = n;
				break;
			case 24:
				n = Flag[arg1] - arg2;
				if(n < 0)
					n = 0;
				Flag[arg1] = n;
				break;
			case 25:
				Put(arg1, arg2);
				break;
			case 26:
				n = Object[arg1];
				Put(arg1, Object[arg2]);
				Put(arg2, n);
				break;
			case 27:
				n = Flag[arg1];
				Flag[arg1] = Flag[arg2];
				Flag[arg2] = n;
				break;
			case 28:
				Means(arg1, arg2);
				break;
			case 29:
				Put(arg1, Object[arg2]);
				break;
			case 30:
				/* Beep */
				putchar('\007');
				fflush(stdout);
				break;
			case 32:
				RamSave(1);
				break;
			case 33:
				RamLoad();
				break;
			case 35:
				Oops();
				break;
			default:
				fprintf(stderr, "Unknown command %d.\n",
					op);
				break;
		}
	}
	while(1);
#ifdef DEBUG
	fprintf(stderr, "\n");
#endif
}

static unsigned char *NextLine(unsigned char *p)
{
	unsigned char op;
	while(!((op = *p) & 0x80)) {
		p+=2;
		if(op > 20)
			p++;
	}
	while(((op = *p) & 0x80)) {
		op &= 0x3F;
		p++;
		if(op > 8)
			p++;
		if(op > 21)
			p++;
	}
	return p;
}

static int FindStatusTable(void)
{
	int pos = 0;
	while((pos = FindCode("\x3E\xFF\x32", pos+1)) != -1) {
		if(Image[pos + 5] != 0x18)
			continue;
		if(Image[pos + 6] != 0x07)
			continue;
		if(Image[pos + 7] != 0x21)
			continue;
		return (Image[pos-2] + (Image[pos-1] << 8)) - 0x4000;
	}
	fprintf(stderr, "Unable to find automatics.\n");
	exit(1);
}

static void RunStatusTable(void) 
{
	unsigned char *p = Image + StatusBase;
	
	ActionsDone = 0;
	ActionsExecuted = 0;
	
	while(*p != 0x7F) {
		ExecuteLineCode(p);
		if(ActionsDone)
			return;
		p = NextLine(p);
	}
}

int FindCommandTable(void)
{
	int pos = 0;
	while((pos = FindCode("\x3E\xFF\x32", pos+1)) != -1) {
		if(Image[pos + 5] != 0x18)
			continue;
		if(Image[pos + 6] != 0x07)
			continue;
		if(Image[pos + 7] != 0x21)
			continue;
		return (Image[pos+8] + (Image[pos+9] << 8)) - 0x4000;
	}
	fprintf(stderr, "Unable to find commands.\n");
	exit(1);
}

static void RunCommandTable(void)
{
	unsigned char *p = Image + ActionBase;

	ActionsDone = 0;
	ActionsExecuted = 0;
	
	while(*p != 0x7F) {
		if((*p == 126 || *p == Word[0]) &&
		   (p[1] == 126 || p[1] == Word[1])) {
#ifdef DEBUG
			PrintWord(p[0]);
			PrintWord(p[1]);
#endif
			ExecuteLineCode(p + 2);
			if(ActionsDone)
				return;
		}
		p = NextLine(p + 2);
	}
}

static int AutoExit(unsigned char v)
{
	unsigned char *p = Image + ExitBase;
	unsigned char want = Location() | 0x80;
	while(*p != want) {
		if(*p == 0xFE)
			return 0;
		p++;
	}
	p++;
	while(*p < 0x80) {
		if(*p == v) {
			Goto(p[1]);
			return 1;
		}
		p+=2;
	}
	return 0;
}
		
static void RunOneInput(void) 
{
	if(Word[0] == 0 && Word[1] == 0) {
		OutCaps();
		Message(11);
		return;
	}
	if(Word[0] < 11) {
		if(AutoExit(Word[0])) {
			if(Redraw)
				Look();
			RunStatusTable();
			return;
		}
	}
	OutCaps();
	RunCommandTable();
	
	if(ActionsExecuted == 0) {
		if(Word[0] < 11)
			Message(24);
		else
			Message(12);
		return;
	}
	if(Redraw)
		Look();
	RunStatusTable();
	if(Redraw)
		Look();
}

static int ParseWord(char *p)
{
	char buf[5];
	int len = strlen(p);
	unsigned char *words = Image + VerbBase;
	int i;

	if(len >= 4) {
		memcpy(buf, p, 4);
		buf[4] = 0;
	} else {
		memcpy(buf, p, len);
		memset(buf + len, ' ', 4 - len);
	}
	for(i = 0; i < 4; i++) {
		if(buf[i] == 0)
			break;
		if(islower(buf[i]))
			buf[i] = toupper(buf[i]);
	}
	while(*words != 126) {
		if(memcmp(words, buf, 4) == 0)
			return words[4];
		words+=5;
	}
	return 0;
}
	
static void  SimpleParser(void) 
{
	int nw;
	int i;
	int wn = 0;
	char wb[5][17];
	char buf[256];
			
	OutChar('\n');
	if(GameVersion > 0) {
		OutCaps();
		Message(14);
	}
	else
		OutString("> ");
	OutFlush();
	do
	{
		LineInput(buf, 255);
		nw = sscanf(buf, "%16s %16s %16s %16s %16s", wb[0], wb[1], wb[2], wb[3], wb[4]);
	} while(nw == 0);		
	
	for(i = 0; i < nw ; i++)
	{
		Word[wn] = ParseWord(wb[i]);
		if(Word[wn])
			wn++;
	}
	for(i = wn; i < 5; i++)
		Word[i] = 0;
}
	
static void FindTables(void) 
{
	TokenBase = FindTokens();
	RoomBase = FindRooms();
	ObjectBase = FindObjects();
	StatusBase = FindStatusTable();
	ActionBase = FindCommandTable();
	ExitBase = FindExits();
	FlagBase = FindFlags();
	ObjLocBase = FindObjectLocations();
	MessageBase = FindMessages();
	Message2Base = FindMessages2();
}

/*
 *	Version 0 is different
 */
 
static int GuessLowObjectEnd0(void)
{
	unsigned char *p = Image + ObjectBase;
	unsigned char *t = NULL;
	unsigned char c = 0, lc;
	int n = 0;
	
	while(1) {
		if(t == NULL)
			t = TokenText(*p++);
		lc = c;
		c = *t & 0x7F;
		if(c == 0x5E || c == 0x7E) {
			if(lc == ',' && n > 20)
				return n;
			n++;
		}
		if(*t++ & 0x80)
			t = NULL;
	}
}


static int GuessLowObjectEnd(void)
{
	unsigned char *p = Image + ObjectBase;
	unsigned char *x;
	int n = 0;
	
	/* Can't automatically guess in this case */
	if (Blizzard)
		return 69;

	if(GameVersion == 0)
		return GuessLowObjectEnd0();
		
	while(n < NumObjects()) {
		while(*p != 0x7E && *p != 0x5E) {
			p++;
		}
		x = TokenText(p[-1]);
		while(!(*x & 0x80)) {
			x++;
		}
		if((*x & 0x7F) == ',')
			return n;
		n++;
		p++;
	}
	fprintf(stderr, "Unable to guess the last description object.\n");
	return 0;
}

int main(int argc, char *argv[])
{
	FILE *f;
	
	if(argv[1] == NULL)
	{
		fprintf(stderr, "%s: <file>.\n", argv[0]);
		exit(1);
	}
	
	f = fopen(argv[1], "r");
	if(f == NULL)
	{
		perror(argv[1]);
		exit(1);
	}
	fseek(f, 27L, 0);
	ImageLen = fread(Image, 1, 131072, f);
	fclose(f);
	
	/* Guess initially at He-man style */
	GameVersion = 2;
	/* Blizzard Pass */
	if(ImageLen > 49152) {
		GameVersion = 1;
		Blizzard = 1;
	}
	/* The message analyser will look for version 0 games */
	
	printf("Loaded %d bytes.\n", ImageLen);
	
	VerbBase = FindCode("NORT\001N", 0);
	if(VerbBase == -1) {
		fprintf(stderr, "No verb table!\n");
		exit(1);
	}

	FindTables();
#ifdef DEBUG
	if (GameVersion != 1)
		Action[12] = "MESSAGE2";
	LoadWordTable();
#endif
	NewGame();
	NumLowObjects = GuessLowObjectEnd();
	DisplayInit();
	RamSave(0);
	Look();
	RunStatusTable();
	if(Redraw)
		Look();
	while(1) {
		Checkpoint();
		SimpleParser();
		RunOneInput();
	}
}
