#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int
getcmd(char *buf, int nbuf)
{
  printf(2, "> ");
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}

void
panic(char *s)
{
  printf(2, "%s\n", s);
  exit();
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

char* strtok(char* str, const char* delims)
{
	static char *tmp, *pstart;
	const char *p_delims;

	if(str!=0) 
	{	
		str[strlen(str)-1]=' ';
		tmp=str;
	}
	if(*tmp=='\0') return 0;

	pstart=tmp;

	while(*tmp!='\0')
	{
		p_delims=delims;

		while(*p_delims!='\0')
		{
			if(*tmp==*p_delims++)
			{
				*tmp++='\0';
				return pstart;
			}
		}

		tmp++;
	}

	return pstart;
}

int proc_made[128];

int main()
{
  int admin_ret=getadmin("2016024893");

  if(admin_ret==-1) exit();

  printf(1, "[Process Manager]\n\n");
	//printf(1, ">");
  static char buf[100];

  while(getcmd(buf, sizeof(buf)) >= 0)
  {
    char* input=strtok(buf, " ");
    char* input0=0;
    char* input1=0;
    char* input2=0;

    int turn=0;
    while(input!=0) 
    {
    	//printf(1,"%s\n", input);
    	if(turn==0) input0=input;
    	if(turn==1) input1=input;
    	if(turn==2) input2=input;
    	turn++;
    	input=strtok(0," ");
	}

	//for(int i=0;i<strlen(input0);i++)
	//{
		//printf(1,"%d input : %c\n", i, input0[i]);
	//}

	if(strcmp(input0,"execute")==0)
	{
		int tmp=atoi(input2);
		int ret=fork1();

		proc_made[ret]=1;

		char* argv[10]={input1,0};
		if(ret==0) exec2(input1,argv,tmp);
	}

	if(strcmp(input0,"kill")==0)
	{
		int pid=atoi(input1);
		int ret=kill(pid);

		if(ret==0) printf(1,"kill %d is success\n", pid);
		else printf(1,"kill %d is failed\n", pid);
	}

	if(strcmp(input0,"memlim")==0)
	{
		int pid=atoi(input1);
		int limit=atoi(input2);

		//printf(1,"pid : %d, limit : %d\n", pid, limit);

		int ret=setmemorylimit(pid,limit);

		if(ret==0) printf(1,"setmemorylimit (process %d) is success\n", pid);
		else printf(1,"setmemorylimit (process %d) is failed\n", pid);
	}

	if(strcmp(input0,"list")==0)
	{
		proc_list();
	}

	if(strcmp(input0,"exit")==0)
	{
		for(int i=0;i<=64;i++)
		{
			if(proc_made[i]==1) 
			{
				kill(i);
				wait();
			}
		}

		exit();
	}

  }
  exit();
}
