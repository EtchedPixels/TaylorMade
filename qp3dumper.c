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

static unsigned char Image[131072];
static size_t ImageLen;
static size_t VerbBase;
static size_t TokenBase;
static size_t MessageBase;
static size_t RoomBase;
static size_t ObjectBase;
static size_t ExitBase;
static size_t ObjLocBase;
static size_t StatusBase;
static size_t ActionBase;
static size_t FlagBase;
static unsigned char *TopText;

static int NumLowObjects;
static int MaxRoom;
static int MaxMessage;
static int MaxMessage2;

static int GameVersion;
static int Blizzard;

/*
 *	  Debugging
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
	"NOTDESTROYED",
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

static int Q3Condition[] = {
	CONDITIONERROR,
	AT,
	NOTAT,
	ATGT,
	ATLT,
	PRESENT,
	ABSENT,
	CARRIED,
	NOTCARRIED,
	NODESTROYED,
	DESTROYED,
	ZERO,
	NOTZERO,
	WORD1,
	WORD2,
	CHANCE,
	LT,
	GT,
	EQ,
	OBJECTAT,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
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
	"OK",		 /* Guess */
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
	"PUT",		  /* ?? */
	"SWAP",
	"SWAPF",
	"MEANS",
	"PUTWITH",
	"BEEP",		   /* Rebel Planet at least */
	"REFRESH?",
	"RAMSAVE",
	"RAMLOAD",
	"CLSLOW?",
	"OOPS",
	"DIAGNOSE",
	"SWITCHINVENTORY",
	"SWITCHCHARACTER",
	"DONE",
	"IMAGE",
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

static int Q3Action[]={
	ACTIONERROR,
	SWITCHINVENTORY, // swap TORCH <-> THING
	DIAGNOSE, // report status
	LOADPROMPT,
	QUIT,
	SHOWINVENTORY,
	ANYKEY,
	SAVE,
	DONE, // set flag 118 to 1?
	GET,
	DROP,
	GOTO,
	SWITCHCHARACTER, // swap TORCH <-> THING
	SET,
	CLEAR,
	MESSAGE,
	CREATE,
	DESTROY,
	LET,
	ADD,
	SUB,
	PUT,
	SWAP,
	IMAGE, // Redraw room image
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0
};


static void LoadWordTable(void)
{
	unsigned char *p = Image + VerbBase;

	printf("Words\n\n");
	while(1) {
		if(p[4] == 255)
			break;
		if(WordMap[p[4]][0] == 0)
			memcpy(WordMap[p[4]], p, 4);
		printf("%-4.4s %d\n", p, p[4]);
		p+=5;
	}
	printf("\n\n");
}

static void PrintWord(unsigned char word)
{
	if(word == 126)
		printf("*	 ");
	else if(word == 0 || WordMap[word][0] == 0)
		printf("%-4d ", word);
	else {
		printf("%c%c%c%c ",
			   WordMap[word][0],
			   WordMap[word][1],
			   WordMap[word][2],
			   WordMap[word][3]);
	}
}


static size_t FindCode(char *x, int base)
{
	unsigned char *p = Image + base;
	size_t len = strlen(x);
	while(p < Image + ImageLen - len) {
		if(memcmp(p, x, len) == 0)
			return p - Image;
		p++;
	}
	return -1;
}

static size_t FindFlags(void)
{
	/* Look for the flag initial block copy */
	size_t pos = FindCode("\xE7\x97\x51\x95\x5B\x7E\x5D\x7E\x76\x93", 0);
	if(pos == -1) {
		fprintf(stderr, "Cannot find initial flag data.\n");
		exit(1);
	}
	return pos + 12;
}

static size_t FindObjectLocations(void)
{
	size_t pos = FindCode("\xF8\x10\x20\x40\xF8\xF8\xFC\xFC\xFC\x01\x05\xFC\x06\xFC\x0B\x18", 0);
	if(pos == -1) {
		fprintf(stderr, "Cannot find initial object data.\n");
		exit(1);
	}
	return pos + 6;
}

static size_t FindExits(void)
{
	size_t pos = FindCode("\x80\x81\x03\x05\x82\x83\x84\x02\x09\x03\x0E\x04\x0F", 0);

	if(pos == -1) {
		fprintf(stderr, "Cannot find initial flag data.\n");
		exit(1);
	}
	return pos;
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

static size_t FindTokens(void)
{
	size_t pos = FindCode("\x58\x58\x58\x58\xFF", 0);
	if(pos == -1) {
		fprintf(stderr, "Unable to find token table.\n");
		exit(1);
	}
	return pos + 6;
}

static unsigned char *TokenText(unsigned char n)
{
	unsigned char *p = Image + TokenBase;

	n -= 0x7b;

	while(n > 0) {
		while((*p & 0x80) == 0)
			p++;
		n--;
		p++;
	}
	return p;
}

static int Upper = 0;

void OutChar(uint8_t c) { // Print character
	if (c == 0x0d)
		return;
	if (Upper && c >= 'a') {
		c -= 0x20; // token is made uppercase
	}
	putchar(c);
	if (c > '!') {
		Upper = 0;
	}
	if (c == '!' || c == '?' || c == ':' || c == '.') {
		Upper = 1;
	}
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

static void PrintText(unsigned char *p, int n)
{
	while (n > 0) {
		while (*p != 0x1f && *p != 0x18) {
			p++;
		}
		n--;
		p++;
	}
	do	{
		if (*p == 0x18)
			return;
		if (*p >= 0x7b) // if c is >= 0x7b it is a token
			PrintToken(*p);
		else {
			OutChar(*p);
		}
	} while (*p++ != 0x1f);
}

static size_t FindMessages(void)
{
	size_t pos = FindCode("\x7F\xF8\x64\x86\xDB\x94\x20\xAD\xD2\x2E\x1F\x66\xE5", 0);


	if(pos == -1) {
		fprintf(stderr, "Unable to locate messages.\n");
		exit(1);
	}
	return pos;
}

static void Message(unsigned int m)
{
	unsigned char *p = Image + MessageBase;
	PrintText(p, m);
}

static size_t FindObjects(void)
{
	size_t pos = FindCode("\x20\xFB\x62\x88\xF4\xAC\xBF\x73\x2C\x18\x20\xFF", 0);
	if(pos == -1) {
		fprintf(stderr, "Unable to locate objects.\n");
		exit(1);
	}
	return pos;
}

static void PrintObject(unsigned char obj)
{
	unsigned char *p = Image + ObjectBase - 1;
	PrintText(p, obj);
}

static size_t FindRooms(void)
{
	size_t pos = FindCode("QUESTPROBE 3: FANTASTIC FOUR", 0);
	if(pos == -1) {
		fprintf(stderr, "Unable to locate rooms.\n");
		exit(1);
	}
	return pos ;
}


static void PrintRoom(unsigned char room)
{
	unsigned char *p = Image + RoomBase;
	PrintText(p, room);
}


static void NewGame(void)
{
	memset(Flag, 0, 128);
	memcpy(Flag + 1, Image + FlagBase, 6);
	memcpy(Object, Image + ObjLocBase, 49);
}

static void ExecuteLineCode(unsigned char *p)
{
	unsigned char arg1 = 0, arg2 = 0;
	do {
		unsigned char op = *p;

		if(op & 0x80)
			break;
		p++;
		arg1 = *p++;
		printf("%s %d ", Condition[Q3Condition[op]], arg1);
		if(op > 15)
		{
			arg2 = *p++;
			printf("%d ", arg2);
		}
		if (op < 5 && arg1 > MaxRoom)
			MaxRoom = arg1;
		if (op == 25 && arg2 > MaxRoom)
			MaxRoom = arg2;
	} while(1);

	do {
		unsigned char op = *p;
		if(!(op & 0x80))
			break;

		printf("%s ", Action[Q3Action[op & 0x3F]]);
		p++;


		if((op & 0x3F) > 8) {
			arg1 = *p++;
			printf("%d ", arg1);
		}
		if((op & 0x3F) > 17) {
			arg2 = *p++;
			printf("%d ", arg2);
		}
		if(op & 0x40)
			printf("DONE");
		op &= 0x3F;

		if (op == 11 && MaxRoom < arg1)
			MaxRoom = arg1;
		if (op == 12 && MaxMessage2 < arg1)
			MaxMessage2 = arg1;
		if (op == 15 && MaxMessage < arg1)
			MaxMessage = arg1;
		if (op == 25 && arg2 < 252 && arg2 > MaxRoom)
			MaxRoom = arg2;

	}
	while(1);
	printf("\n");
}

static unsigned char *NextLine(unsigned char *p)
{
	unsigned char op;
	while(!((op = *p) & 0x80)) {
		p+=2;
		if(op > 15)
			p++;
	}
	while(((op = *p) & 0x80)) {
		op &= 0x3F;
		p++;
		if (op > 8)
			p++;
		if (op > 17) {
			p++;
		}

	}
	return p;
}

static size_t FindStatusTable(void)
{
	size_t pos = FindCode("\x7E\x7E\x01\x02\x0C\x30\x0B\x17\x10\x16\x07\x05", 0);
	if (pos == -1) {
		fprintf(stderr, "Unable to find automatics.\n");
		exit(1);
	}
	return pos;
}

static void DumpStatusTable(void)
{
	unsigned char *p = Image + StatusBase;

	while(*p != 0x7F) {
		while (*p == 0x7e) {
			p++;
		}
		ExecuteLineCode(p);
		p = NextLine(p);
	}
	printf("\n\n");
}

size_t FindCommandTable(void)
{
	size_t pos = FindCode("\x19\x10\x01\x06\x8B\x02\x8E\x1B\x91\x12\xD0\x11", 0);
	if (pos == -1) {
		fprintf(stderr, "Unable to find commands.\n");
		exit(1);
	}
	return pos;
}

static void DumpCommandTable(void)
{
	unsigned char *p = Image + ActionBase;

	p = NextLine(p);

	while(*p != 0x7F) {
		PrintWord(p[0]);
		PrintWord(p[1]);
		ExecuteLineCode(p + 2);
		p = NextLine(p + 2);
	}
	printf("\n\n");
}

static int DumpExits(unsigned char v)
{
	unsigned char *p = Image + ExitBase;
	unsigned char want = v | 0x80;
	while(*p != want) {
		if(*p == 0xFE)
			return 0;
		p++;
	}
	p++;
	while(*p < 0x80) {
		printf("  ");
		PrintWord(*p);
		printf(" %d\n", p[1]);
		p+=2;
	}
	return 0;
}

static void FindTables(void)
{
	printf("Verbs at %04lX\n", VerbBase + 0x4000);
	TokenBase = FindTokens();
	printf("Tokens at %04lX\n", TokenBase + 0x4000);
	RoomBase = FindRooms();
	printf("Rooms at %04lX\n", RoomBase + 0x4000);
	ObjectBase = FindObjects();
	printf("Objects at %04lX\n", ObjectBase + 0x4000);
	StatusBase = FindStatusTable();
	printf("Status at %04lX\n", StatusBase + 0x4000);
	ActionBase = FindCommandTable();
	printf("Actions at %04lX\n", ActionBase + 0x4000);
	ExitBase = FindExits();
	printf("Exits at %04lX\n", ExitBase + 0x4000);
	FlagBase = FindFlags();
	printf("Flags at %04lX\n", FlagBase + 0x4000);
	ObjLocBase = FindObjectLocations();
	printf("Object Locations at %04lX\n", ObjLocBase + 0x4000);
	MessageBase = FindMessages();
	MaxMessage = 238;
	printf("Messages at %04lX\n", MessageBase + 0x4000);
	printf("\n\n\n");
}

static int GuessLowObjectEnd(void)
{
	return 49;
}

void DumpMessages(void)
{
	int i;
	printf("%d Messages\n\n", MaxMessage);
	for (i = 0; i < MaxMessage; i++) {
		printf("%d: ", i);
		Message(i);
		printf("\n");
	}
	printf("\n\n");
}

void DumpObjects(void)
{
	int n = 49;
	int i;

	printf("%d Objects\n\n", n);
	for (i = 0; i < n; i++) {
		printf("%d: ", i);
		Upper = 1;
		PrintObject(i);
		printf(" %d\n", (int)Object[i]);
	}
	printf("\n\n");
}

static int NumRooms(void)
{
	return 39;
}

void DumpRooms(void)
{
	int n = NumRooms();
	int i;

	for (i = 0; i < n; i++) {
		printf("%d: ", i);
		PrintRoom(i);
		printf("\n");
		DumpExits(i);
		printf("\n");
	}
}

void PrintConditionAddresses(void) {
	fprintf(stderr, "Memory adresses of conditions\n\n");
	uint16_t conditionsOffsets = 0x96a6 - 0x4000 + 2;
	uint8_t *conditions;
	conditions = &Image[conditionsOffsets];
	for (int i = 1; i < 20; i++) {
		uint16_t address = *conditions++;
		address += *conditions * 256;
		conditions++;
		fprintf(stderr, "Condition %02d: 0x%04x (%s)\n", i, address, Condition[Q3Condition[i]]);
	}
	fprintf(stderr, "\n");
}

void PrintActionAddresses(void) {
	fprintf(stderr, "Memory adresses of actions\n\n");
	uint16_t actionOffsets = 0x991a - 0x4000 + 2;
	uint8_t *actions;
	actions = &Image[actionOffsets];
	for (int i = 1; i < 24; i++) {
		uint16_t address = *actions++;
		address += *actions * 256;
		actions++;
		fprintf(stderr, "   Action %02d: 0x%04x (%s)\n", i, address, Action[Q3Action[i]]);
	}
	fprintf(stderr, "\n");
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
		Blizzard = 1;
		GameVersion = 1;
	}
	/* The message analyser will look for version 0 games */

	printf("Loaded %zu bytes.\n", ImageLen);

	VerbBase = FindCode("NORT\001N", 0);
	if(VerbBase == -1) {
		fprintf(stderr, "No verb table!\n");
		exit(1);
	}

	FindTables();
	LoadWordTable();
	NewGame();
	NumLowObjects = GuessLowObjectEnd();
	DumpObjects();
	DumpStatusTable();
	DumpCommandTable();
	DumpRooms();
	DumpMessages();
	PrintConditionAddresses();
	PrintActionAddresses();
	printf("Text End: %04lX\n", TopText - Image + 0x4000);
	printf("Working size needed: %04lX\n", TopText - Image - ExitBase);
}
