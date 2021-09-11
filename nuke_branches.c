#include <stdio.h>      // stderr, printf, fprintf
#include <fcntl.h>      // open, O_RDONLY
#include <unistd.h>     // read
#include <termios.h>    // termios, tcgetattr, tcsetattr
#include <string.h>     // strncmp

typedef struct termios termios;

#define ArrayCount(Array) (sizeof(Array)/sizeof((Array)[0]))

static char Memory[1024 * 1024];
static int ConsoleHandle;
static char *Options[100];
static int OptionCount;

enum key
{
    Key_Unsupported,
    Key_Escape,
    Key_Up,
    Key_Down,
    Key_Left,
    Key_Right,
    Key_Delete,
    Key_Enter,
    Key_Q,
};
typedef enum key key;

enum clear_codes
{
    CLEAR_FROM_CURSOR_TO_END,
    CLEAR_FROM_CURSOR_TO_BEGIN,
    CLEAR_ALL,
};

static key
GetKey()
{
    key Result = Key_Unsupported;

    // NOTE(chuck): Annoying thing for disabling buffering and echo.
    termios OldSettings;
    termios NewSettings;
    tcgetattr(0, &OldSettings);
    NewSettings = OldSettings;
    NewSettings.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(0, TCSANOW, &NewSettings);

    char Buffer[4] = {};
    /* NOTE(chuck): Not using getchar() because it only reads a single
       character at a time.  If you call it too much, you hang while it
       waits for more input.  It's difficult to disambiguate with that
       because, e.g. ESC is the single byte 27 and arrow keys are the
       byte 27 followed by other codes, etc.
       
        TODO(chuck): Is there a more streamlined way to do this?  Like
        without having to parse multi-byte sequences? */
    int BytesRead = read(ConsoleHandle, Buffer, 4);

    // printf("%d bytes ", BytesRead);
    // for(int Index = 0;
    //     Index < BytesRead;
    //     ++Index)
    // {
    //     printf("%d ", Buffer[Index]);
    // }
    // printf("\n");

    if(BytesRead)
    {
        if(Buffer[0] == 27)
        {
            if(BytesRead == 1)
            {
                Result = Key_Escape;
            }
            else
            {
                if(Buffer[1] == 91)
                {
                    if(BytesRead == 3)
                    {
                        if(Buffer[2] == 65)
                        {
                            Result = Key_Up;
                        }
                        else if(Buffer[2] == 66)
                        {
                            Result = Key_Down;
                        }
                        else if(Buffer[2] == 68)
                        {
                            Result = Key_Left;
                        }
                        else if(Buffer[2] == 67)
                        {
                            Result = Key_Right;
                        }
                    }
                    else if(BytesRead == 4)
                    {
                        if(Buffer[2] == 51 && Buffer[3] == 126)
                        {
                            Result = Key_Delete;
                        }
                    }
                }
            }
        }
        else if(BytesRead == 1)
        {
            if(Buffer[0] == 10)
            {
                Result = Key_Enter;
            }
            else if(Buffer[0] == 113)
            {
                Result = Key_Q;
            }
        }
    }
    else
    {
        fprintf(stderr, "Failed to ready from the input buffer.\n");
    }

    tcsetattr(0, TCSANOW, &OldSettings);

    return(Result);
}

static void
PrintMenu(int SelectedIndex)
{
    printf("\x1b[%dJ", CLEAR_ALL);
    printf("%d branches.  Press DELETE to delete the selected branch.  Press Q or ESC to exit.\n", OptionCount);
    for(int Index = 0;
        Index < OptionCount;
        ++Index)
    {
        if(Index == SelectedIndex)
        {
            printf("  > ");
        }
        else
        {
            printf("    ");
        }
        printf("%s", Options[Index]);
    }
}

static void
GetBranches()
{
    FILE *GitBranchHandle = popen("/usr/bin/git branch", "r");
    char *Line = Memory;
    OptionCount = 0;
    while(fgets(Line, 1024, GitBranchHandle) && OptionCount < ArrayCount(Options))
    {
        char *P = Line;
        while(*P && ((*P == ' ') || (*P == '*')))
        {
            ++P;
        }
        if(strncmp("master\n", P, 7) && strncmp("main\n", P, 5))
        {
            Options[OptionCount++] = P;
            Line += 1024;
        }
    }
    pclose(GitBranchHandle);
}

int
main(int ArgCount, char **Args)
{
    int Result = 0;

    ConsoleHandle = open("/dev/tty", O_RDONLY);

    GetBranches();
    if(OptionCount)
    {
        PrintMenu(0);

        int SelectedIndex = 0;
        int Running = 1;
        while(Running)
        {
            key Key = GetKey();
            switch(Key)
            {
                case Key_Q:
                case Key_Escape:
                {
                    Running = 0;
                } break;

                case Key_Up:
                case Key_Left:
                {
                    if(--SelectedIndex < 0)
                    {
                        SelectedIndex += OptionCount;
                    }
                    PrintMenu(SelectedIndex);
                } break;

                case Key_Down:
                case Key_Right:
                {
                    SelectedIndex = (SelectedIndex + 1) % OptionCount;
                    PrintMenu(SelectedIndex);
                } break;

                case Key_Delete:
                {
                    char *P = Options[SelectedIndex];
                    while(*P && *P == ' ')
                    {
                        ++P;
                    }
                    
                    char DeleteBranchCommand[1024] = {};
                    sprintf(DeleteBranchCommand, "/usr/bin/git branch -D %s", P);
                    FILE *GitDeleteHandle = popen(DeleteBranchCommand, "r");
                    if(GitDeleteHandle)
                    {
                        pclose(GitDeleteHandle);

                        --OptionCount;
                        if(SelectedIndex >= OptionCount)
                        {
                            --SelectedIndex;
                        }

                        GetBranches();
                        PrintMenu(SelectedIndex);
                    }
                } break;
            }
        }
    }
    else
    {
        fprintf(stderr, "There are no branches other than master.\n");
        Result = 1;
    }

    return(Result);
}