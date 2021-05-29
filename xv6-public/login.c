#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define maxlen 10

int
getcmd(char *buf, int nbuf)
{
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}

char* strtok(char* str, const char* delims)
{
	static char *tmp, *pstart;
	const char *p_delims;

	if(str!=0) 
	{	
		//str[strlen(str)-1]=' ';
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

char *argv[] = {"sh", 0 };
//char *tmp;
char *user[20];
int user_cnt=0;
char ref[512]; 

int main()
{
	int fd, n;
	char *buf="root 1234 ";
	//char *test="abcd e1234\n";
	//int fd;
  	if((fd=open("userinfo",O_RDONLY))<0)
  	{
    	//printf(1,"I have to made it in init\n");
    	close(fd);
        fd=open("userinfo",0x202);
    	int w=write(fd,buf,strlen(buf));
    	//write(fd,0,0);
    	//int w1=write(fd,test,strlen(test));
    	if (w<0) printf(1,"write error in login\n");
        close(fd);
 	}
 	fd=open("userinfo",O_RDONLY);
 	while((n = read(fd, ref, sizeof(ref))) > 0) {
        //printf(1,"size of ref %d\n", strlen(ref));
        user[0]=strtok(ref," ");
        user_cnt++;
        for(int i=1;;i++)
        {
            user[i]=strtok(0," ");
            if(user[i]==0) break;
            user_cnt++;
        }
    }
    close(fd);

    while(1)
    {
    	int pass=0;
    	printf(1,"username: ");
    	char ID[15], pwd[15];
    	getcmd(ID, sizeof(ID));
    	ID[strlen(ID)-1]=0;
    	printf(1,"password: ");
    	getcmd(pwd, sizeof(pwd));
    	pwd[strlen(pwd)-1]=0;
    	
    	for(int i=0;i<user_cnt;i+=2)
    	{
    		//printf(1,"id:%s passwd:%s\n", user[i], user[i+1]);
            if(strcmp(ID,user[i])==0&&strcmp(pwd,user[i+1])==0)
    		{
    			pass=1;
                enrolluser(i/2);
    			break;
    		}
    	}
    	if(pass==1) break;
    }
    //strcmp(ID,"root")==0&&strcmp(pwd,"1234")==0
    //printf(1,"n=%d\n", n);
	int pid;

	pid = fork();
    if(pid < 0){
      printf(1, "login: fork failed\n");
      exit();
    }
  
    if(pid == 0){

      exec("sh", argv);
      printf(1, "login: exec sh failed\n");
    }
    wait();
    exit();
}