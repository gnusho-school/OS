#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

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

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

char *user[20];
int user_cnt=0;
char ref[512]; 

void
ls(char *path)
{
  char buf[512], *p;
  int fd, n;
  struct dirent de;
  struct stat st;

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

  //for(int i=0;i<user_cnt;i++) printf(2,"in ls.c user[%d]: %s\n", i, user[i]);

  if((fd = open(path, 0)) < 0){
    printf(2, "ls: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    printf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_FILE:
    printf(1, "%s ", fmtname(buf));
    printf(1, "-");
    int tmp=st.mode;
    int k=32;
    while(k)
    {
      if(tmp/k==1)
      {
        if(k==32||k==4) printf(1, "r");
        if(k==16||k==2) printf(1,"w");
        if(k==8||k==1) printf(1,"x");

        tmp-=k;
      }
      else printf(1,"-");
      k/=2;
    }
    printf(1, "%s %d %d %d %d\n", user[st.owner*2], st.type, st.ino, st.size);
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf(1, "ls: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){
        printf(1, "ls: cannot stat %s\n", buf);
        continue;
      }
      printf(1, "%s ", fmtname(buf));
      int isconsole=0;
      if(st.type==T_FILE) printf(1, "-");
      else if(st.type==3) 
      {
        isconsole=1;
        printf(1, "c");
      }
      else printf(1, "d");
      int tmp=st.mode;
      //printf(1, "%d ", tmp);
      int k=32;
      while(k)
      {
        if(tmp/k==1&&!isconsole)
        {
          if(k==32||k==4) printf(1, "r");
          if(k==16||k==2) printf(1,"w");
          if(k==8||k==1) printf(1,"x");

          tmp-=k;
        }
        else printf(1,"-");
        k/=2;
      }
      
      if(!isconsole) printf(1, " %s %d %d %d\n", user[st.owner*2], st.type, st.ino, st.size);
      else printf(1, " %d %d %d\n", st.type, st.ino, st.size);
      //break;
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    ls(".");
    exit();
  }
  for(i=1; i<argc; i++)
    ls(argv[i]);
  exit();
}
