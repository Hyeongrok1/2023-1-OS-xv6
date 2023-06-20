# xv6_project
xv6 운영체제는 MIT에서 개발한 Unix-like 교육용 운영체제입니다.
성균관대 운영체제 수업에서 과제용으로 사용되었습니다.

## Project 0 - Booting (10/10) pt
Project 0의 내용은 간단하게 xv6 운영체제를 실행하고 boot message를 추가하는 것입니다.<br>
xv6를 실행하기 위해서는 오픈소스 에뮬레이터인 QEMU를 설치해야 합니다. <br>

## Project 1 - System Call (15/15) pt
Project 1의 내용은 xv6에 System Call을 추가하는 것입니다. <br>
이후 과제에서 새로운 System Call을 추가하기 위한 초석입니다. <br>
해당 과제에서 추가한 System Call은 다음과 같습니다.
* getnice
* setnice
* ps <br>

## Project 2 - CFS (25/25) pt
Project 2의 내용은 CFS를 구현하는 것입니다.<br>
CFS란 Completely Fair Scheduler의 약자로, 현재 Linux의 기본 Scheduler입니다. <br>
xv6의 기본 scheduler는 Round-Robin 방식으로 타이머로 process가 CPU를 포기하도록 만듭니다. <br>
CFS와의 차이점은 기본 Scheduler는 process의 우선 순위를 메길 수 없다는 것입니다. <br><br>
CFS는 프로세스에 할당된 weight에 따라 CPU를 할당합니다. 발전된 Stride scheduling이라고 보면 편합니다.<br>
각 nice 값에 따라 weight를 하드 코딩을 통해 배열로 관리하고,<br> 각 프로세스의 time slice는 scheduling_latency(6ms) * weight/total weight of runqueue 로 계산합니다.<br>
vruntime은 각 프로세스가 weight에 비례해서 얼마나 실행되었는지 나타내는 값으로, process가 공평하게 CPU를 할당받았는지 비교하기 위해 쓰입니다.<br>

## Project 3- Virtual Memory (25/25) pt
Project 3의 내용은 mmap(), munmap(), freemem() 시스템 콜, Page fault handler를 구현하는 것입니다. <br>
* uint mmap(uint addr, int length, int prot, int flags, int fd, int offset)
* munmap(addr)
* int freemem()

mmap 시스템 콜은 특정 길이의 page를 할당 받아서 시작 주소를 반환하는 시스템 콜입니다. fd에 file descriptor를 넣으면 해당 파일이 메모리 공간에 할당됩니다. <br>
munmap 시스템 콜은 mmap으로 할당된 메모리 공간을 풀어주는 기능을 합니다. <br>
freemem 시스템 콜은 사용하지 않는 page 개수를 세서 반환하는 시스템 콜입니다. <br>

Page fault handler는 가상 메모리에 실제 physical 페이지와 page table이 매핑되어 있지 않을 상태로 주소가 접근될 경우, physical 페이지와 page table을 실제로 할당하는 역할을 합니다. <br>

그 외에 fork로 새 프로세스가 생성되면 부모 프로세스의 mmap 상태를 그대로 복사합니다. 

Project 3까지 만점을 받았습니다. 학기 중에는 다른 학생들의 표절을 막기 위해 repository를 private으로 바꿀 것입니다.
