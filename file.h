struct file {
  /*
  FD_NONE : 빈 파일 디스크립터
  FD_PIPE : 파이프를 가리키는 디스크립터
  FD_INODE : 파일을 가리키는 디스크립터
  */
  enum { FD_NONE, FD_PIPE, FD_INODE } type;
  
  int ref; // 참조 횟수
  char readable; // 0 : 불가능  1 : 가능 
  char writable; // 0 : 불가능  1: 가능
  struct pipe *pipe; // 파이프의 경우, 파이프에 대한 포인터
  struct inode *ip; // 파일의 경우, 파일에 대한 포인터
  uint off; // 파일 디스크립터의 현재 OFFSET (읽거나 쓸때 사용된다)
};


// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};

// table mapping major device number to
// device functions
struct devsw {
  int (*read)(struct inode*, char*, int);
  int (*write)(struct inode*, char*, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
