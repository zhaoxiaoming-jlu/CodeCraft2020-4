#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <stack>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#define U32 uint64_t
#define P3(x) ((x << 1) + x)
#define P5(x) ((x << 2) + x)
#define P10(x) ((x << 3) + (x << 1))

#ifdef LOCAL
#define TRAIN "./data/big/test_data.txt"
#define RESULT "./data/big/result.txt"
#else
#define TRAIN "/data/test_data.txt"
#define RESULT "/projects/student/result.txt"
#endif

/*
 * 常量
 */
const U32 MAXEDGE = 3000000 + 7;  // 边
const U32 MAXN = 3000000 + 7;     // 结点
const U32 MAXM = 20000000 + 7;    // 环
const U32 NTHREAD = 8;            // 线程
const U32 NUMLEN = 12;            // 数字最大长度
const U32 PARAM[NTHREAD] = {1, 2, 3, 4, 5, 6, 7, 8};

struct PreBuffer {
  char str[NUMLEN];
  U32 len;
};
/*
 * 进程同步共享变量
 */
U32 TotalAnswers = shmget(IPC_PRIVATE, 4, IPC_CREAT | 0600);        // share
U32 FlagFork = shmget(IPC_PRIVATE, NTHREAD * 4, IPC_CREAT | 0600);  // share
U32 FlagSave = shmget(IPC_PRIVATE, 4, IPC_CREAT | 0600);            // share
U32 *TotalAnswersPtr;  // share answers
U32 *FlagForkPtr;      // share flag
U32 *FlagSavePtr;      // share save

/*
 * 计数变量
 */
U32 MaxID = 0;              // 最大点
U32 answers = 0;            // 环个数
U32 EdgesCount = 0;         // 边数目
U32 JobsCount = 0;          // 有效点数目
U32 IDDomCount = 0;         // ID数目
U32 ThEdgesCount[NTHREAD];  // 线程边数目
U32 ReachablePointCount;    // 反向可达数目
U32 AnswerLength0 = 0;      // 长度为3的环
U32 AnswerLength1 = 0;      // 长度为4的环
U32 AnswerLength2 = 0;      // 长度为5的环
U32 AnswerLength3 = 0;      // 长度为6的环
U32 AnswerLength4 = 0;      // 长度为7的环
U32 ThreeLength = 0;        // 3环长度

/*
 * 找环用
 */
char *ThreeCircle = new char[NUMLEN * 3];  // 长度为3的环
char *Reachable = new char[MAXN];          // 反向标记可达
U32 ReachablePoint[MAXN];                  // 反向可达点
U32 LastWeight[MAXN];                      // 最后一层权重

/*
 * 存结果
 */
char *Answer0 = new char[MAXM * NUMLEN * 3];  // 长度为3的环
char *Answer1 = new char[MAXM * NUMLEN * 4];  // 长度为4的环
char *Answer2 = new char[MAXM * NUMLEN * 5];  // 长度为5的环
char *Answer3 = new char[MAXM * NUMLEN * 6];  // 长度为6的环
char *Answer4 = new char[MAXM * NUMLEN * 7];  // 长度为7的环

/*
 * 图信息
 */
U32 Edges[MAXEDGE][3];                            // 所有边
U32 ThEdges[NTHREAD][MAXEDGE / NTHREAD + 7][3];   // 线程边
U32 Jobs[MAXN];                                   // 有效点
U32 IDDom[MAXN];                                  // ID集合
PreBuffer MapID[MAXN];                            // 预处理答案
std::vector<std::pair<U32, U32>> Children[MAXN];  // 子结点
std::vector<std::pair<U32, U32>> Parents[MAXN];   // 父节点

inline void GetShmPtr() {
  TotalAnswersPtr = (U32 *)shmat(TotalAnswers, NULL, 0);
  FlagForkPtr = (U32 *)shmat(FlagFork, NULL, 0);
  FlagSavePtr = (U32 *)shmat(FlagSave, NULL, 0);
}

void ParseInteger(const U32 &x) {
  U32 num = IDDom[x];
  auto &mpid = MapID[x];
  if (num == 0) {
    mpid.str[0] = '0';
    mpid.str[1] = ',';
    mpid.len = 2;
  } else {
    char tmp[NUMLEN];
    int idx = NUMLEN;
    tmp[--idx] = ',';
    while (num) {
      tmp[--idx] = num % 10 + '0';
      num /= 10;
    }
    memcpy(mpid.str, tmp + idx, NUMLEN - idx);
    mpid.len = NUMLEN - idx;
  }
}

void HandleLoadData(int pid, int st, int ed) {
  auto &E = ThEdges[pid];
  auto &count = ThEdgesCount[pid];
  for (int i = st; i < ed; ++i) {
    const auto &e = Edges[i];
    E[count][0] = std::lower_bound(IDDom, IDDom + MaxID, e[0]) - IDDom;
    E[count][1] = std::lower_bound(IDDom, IDDom + MaxID, e[1]) - IDDom;
    E[count++][2] = e[2];
  }
}

void LoadData() {
  std::cerr << "loadstart\n";
  U32 fd = open(TRAIN, O_RDONLY);
  U32 bufsize = lseek(fd, 0, SEEK_END);
  char *buffer = (char *)mmap(NULL, bufsize, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  const char *ptr = buffer;
  U32 u = 0, v = 0, w = 0;
  while (ptr - buffer < bufsize) {
    while (*ptr != ',') {
      u = P10(u) + *ptr - '0';
      ++ptr;
    }
    ++ptr;
    while (*ptr != ',') {
      v = P10(v) + *ptr - '0';
      ++ptr;
    }
    ++ptr;
    while (*ptr != '\r' && *ptr != '\n') {
      w = P10(w) + *ptr - '0';
      ++ptr;
    }
    if (*ptr == '\r') ++ptr;
    ++ptr;
    Edges[EdgesCount][0] = u;
    Edges[EdgesCount][1] = v;
    Edges[EdgesCount++][2] = w;
    IDDom[IDDomCount++] = u;
    IDDom[IDDomCount++] = v;
    u = v = w = 0;
  }
  std::sort(IDDom, IDDom + IDDomCount);
  MaxID = std::unique(IDDom, IDDom + IDDomCount) - IDDom;
  int st = 0, block = EdgesCount / NTHREAD;
  std::thread Th[NTHREAD];
  for (int i = 0; i < NTHREAD; ++i) {
    int ed = (i == NTHREAD - 1 ? EdgesCount : st + block);
    Th[i] = std::thread(HandleLoadData, i, st, ed);
    st = ed;
  }
  for (int i = 0; i < NTHREAD; ++i) Th[i].join();
  for (int i = 0; i < NTHREAD; ++i) {
    const auto &E = ThEdges[i];
    const auto &count = ThEdgesCount[i];
    for (int j = 0; j < count; ++j) {
      const auto &e = E[j];
      Children[e[0]].emplace_back(std::make_pair(e[1], e[2]));
      Parents[e[1]].emplace_back(std::make_pair(e[0], e[2]));
    }
  }
  for (int i = 0; i < MaxID; ++i) {
    if (!Children[i].empty() && !Parents[i].empty()) {
      Jobs[JobsCount++] = i;
      ParseInteger(i);
      std::sort(Children[i].begin(), Children[i].end());
    }
  }
#ifdef LOCAL
  std::cerr << "@ u: " << MaxID << ", e: " << EdgesCount << "\n";
#endif
}

void BackSearch(const U32 &job) {
  for (int i = 0; i < ReachablePointCount; ++i) {
    Reachable[ReachablePoint[i]] = 0;
  }
  ReachablePointCount = 0;
  ReachablePoint[ReachablePointCount++] = job;
  Reachable[job] = 7;
  for (const auto &it1 : Parents[job]) {
    const U32 &v1 = it1.first, &w1 = it1.second;
    if (v1 <= job) continue;
    LastWeight[v1] = w1;
    Reachable[v1] = 7;
    ReachablePoint[ReachablePointCount++] = v1;
    for (const auto &it2 : Parents[v1]) {
      const U32 &v2 = it2.first, &w2 = it2.second;
      if (v2 <= job) continue;
      if (w2 > P5(w1) || w1 > P3(w2)) continue;
      Reachable[v2] |= 6;
      ReachablePoint[ReachablePointCount++] = v2;
      for (const auto &it3 : Parents[v2]) {
        const U32 &v3 = it3.first, &w3 = it3.second;
        if (v3 <= job || v3 == v1) continue;
        if (w3 > P5(w2) || w2 > P3(w3)) continue;
        Reachable[v3] |= 4;
        ReachablePoint[ReachablePointCount++] = v3;
      }
    }
  }
}

void ForwardSearch(U32 st) {
  const auto &mpid0 = MapID[st];
  memcpy(ThreeCircle, mpid0.str, mpid0.len);
  U32 idx0 = mpid0.len, ans = 0;
  for (const auto &it1 : Children[st]) {
    const U32 &v1 = it1.first, &w1 = it1.second;
    if (v1 < st) continue;
    const auto &mpid1 = MapID[v1];
    memcpy(ThreeCircle + idx0, mpid1.str, mpid1.len);
    U32 idx1 = idx0 + mpid1.len;
    U32 P5w1 = P5(w1);
    for (const auto &it2 : Children[v1]) {
      const U32 &v2 = it2.first, &w2 = it2.second;
      if (v2 <= st || w2 > P3(w1) || w1 > P5(w2)) continue;
      const auto &mpid2 = MapID[v2];
      memcpy(ThreeCircle + idx1, mpid2.str, mpid2.len);
      ThreeLength = idx1 + mpid2.len;
      for (const auto &it3 : Children[v2]) {
        const U32 &v3 = it3.first, &w3 = it3.second;
        if (v3 < st || v3 == v1 || w3 > P3(w2) || w2 > P5(w3)) {
          continue;
        }
        if (v3 == st) {
          if (w3 <= P5w1 && P3(w3) >= w1) {
            memcpy(Answer0 + AnswerLength0, ThreeCircle, ThreeLength);
            AnswerLength0 += ThreeLength;
            *(Answer0 + AnswerLength0 - 1) = '\n';
            ++ans;
          }
          continue;
        }
        const auto &mpid3 = MapID[v3];
        for (const auto &it4 : Children[v3]) {
          const U32 &v4 = it4.first, &w4 = it4.second;
          if (!(Reachable[v4] & 4) || w4 > P3(w3) || w3 > P5(w4)) {
            continue;
          }
          if (v4 == st) {
            if (w4 <= P5w1 && P3(w4) >= w1) {
              memcpy(Answer1 + AnswerLength1, ThreeCircle, ThreeLength);
              AnswerLength1 += ThreeLength;
              memcpy(Answer1 + AnswerLength1, mpid3.str, mpid3.len);
              AnswerLength1 += mpid3.len;
              *(Answer1 + AnswerLength1 - 1) = '\n';
              ++ans;
            }
            continue;
          }
          if (v1 == v4 || v2 == v4) {
            continue;
          }
          const auto &mpid4 = MapID[v4];
          for (const auto &it5 : Children[v4]) {
            const U32 &v5 = it5.first, &w5 = it5.second;
            if (!(Reachable[v5] & 2) || w5 > P3(w4) || w4 > P5(w5)) {
              continue;
            }
            if (v5 == st) {
              if (w5 <= P5w1 && P3(w5) >= w1) {
                memcpy(Answer2 + AnswerLength2, ThreeCircle, ThreeLength);
                AnswerLength2 += ThreeLength;
                memcpy(Answer2 + AnswerLength2, mpid3.str, mpid3.len);
                AnswerLength2 += mpid3.len;
                memcpy(Answer2 + AnswerLength2, mpid4.str, mpid4.len);
                AnswerLength2 += mpid4.len;
                *(Answer2 + AnswerLength2 - 1) = '\n';
                ++ans;
              }
              continue;
            }
            if (v1 == v5 || v2 == v5 || v3 == v5) {
              continue;
            }
            const auto &mpid5 = MapID[v5];
            for (const auto &it6 : Children[v5]) {
              const U32 &v6 = it6.first, &w6 = it6.second;
              if (!(Reachable[v6] & 1) || w6 > P3(w5) || w5 > P5(w6)) {
                continue;
              }
              if (v6 == st) {
                if (w6 <= P5w1 && P3(w6) >= w1) {
                  memcpy(Answer3 + AnswerLength3, ThreeCircle, ThreeLength);
                  AnswerLength3 += ThreeLength;
                  memcpy(Answer3 + AnswerLength3, mpid3.str, mpid3.len);
                  AnswerLength3 += mpid3.len;
                  memcpy(Answer3 + AnswerLength3, mpid4.str, mpid4.len);
                  AnswerLength3 += mpid4.len;
                  memcpy(Answer3 + AnswerLength3, mpid5.str, mpid5.len);
                  AnswerLength3 += mpid5.len;
                  *(Answer3 + AnswerLength3 - 1) = '\n';
                  ++ans;
                }
                continue;
              }
              const U32 &w7 = LastWeight[v6];
              if (v1 == v6 || v2 == v6 || v3 == v6 || v4 == v6 || w7 > P3(w6) ||
                  w6 > P5(w7)) {
                continue;
              }
              const auto &mpid6 = MapID[v6];
              if (w7 <= P5w1 && P3(w7) >= w1) {
                memcpy(Answer4 + AnswerLength4, ThreeCircle, ThreeLength);
                AnswerLength4 += ThreeLength;
                memcpy(Answer4 + AnswerLength4, mpid3.str, mpid3.len);
                AnswerLength4 += mpid3.len;
                memcpy(Answer4 + AnswerLength4, mpid4.str, mpid4.len);
                AnswerLength4 += mpid4.len;
                memcpy(Answer4 + AnswerLength4, mpid5.str, mpid5.len);
                AnswerLength4 += mpid5.len;
                memcpy(Answer4 + AnswerLength4, mpid6.str, mpid6.len);
                AnswerLength4 += mpid6.len;
                *(Answer4 + AnswerLength4 - 1) = '\n';
                ++ans;
              }
            }
          }
        }
      }
    }
  }
  answers += ans;
}

void FindCircle(U32 pid) {
  U32 sum = 0;
  for (int i = 0; i < NTHREAD; ++i) sum += PARAM[i];
  U32 st = 0, block = JobsCount / sum;
  for (int i = 0; i < pid; ++i) st += block * PARAM[i];
  U32 ed = (pid == NTHREAD - 1 ? JobsCount : st + block * PARAM[pid]);
  for (U32 i = st; i < ed; ++i) {
    const U32 &job = Jobs[i];
    BackSearch(job);
    ForwardSearch(job);
  }
  *TotalAnswersPtr += answers;
  FlagForkPtr[pid] = 1;
}

void SaveAnswer(U32 pid) {
  U32 answers = *TotalAnswersPtr;
  FILE *fp;

  if (pid == 0) {
    char firBuf[NUMLEN];
    U32 firIdx = NUMLEN;
    firBuf[--firIdx] = '\n';
    if (answers == 0) {
      firBuf[--firIdx] = '0';
    } else {
      while (answers) {
        firBuf[--firIdx] = answers % 10 + '0';
        answers /= 10;
      }
    }

    fp = fopen(RESULT, "w");
    fwrite(firBuf + firIdx, 1, NUMLEN - firIdx, fp);
    fclose(fp);
    *FlagSavePtr = 0;
  }

  while (*FlagSavePtr != pid * 5) usleep(1);
  fp = fopen(RESULT, "at");
  fwrite(Answer0, 1, AnswerLength0, fp);
  fclose(fp);
  if (pid == NTHREAD - 1) {
    *FlagSavePtr = 1;
  } else {
    *FlagSavePtr = (pid + 1) * 5;
  }

  while (*FlagSavePtr != pid * 5 + 1) usleep(1);
  fp = fopen(RESULT, "at");
  fwrite(Answer1, 1, AnswerLength1, fp);
  fclose(fp);
  if (pid == NTHREAD - 1) {
    *FlagSavePtr = 2;
  } else {
    *FlagSavePtr = (pid + 1) * 5 + 1;
  }

  while (*FlagSavePtr != pid * 5 + 2) usleep(1);
  fp = fopen(RESULT, "at");
  fwrite(Answer2, 1, AnswerLength2, fp);
  fclose(fp);
  if (pid == NTHREAD - 1) {
    *FlagSavePtr = 3;
  } else {
    *FlagSavePtr = (pid + 1) * 5 + 2;
  }

  while (*FlagSavePtr != pid * 5 + 3) usleep(1);
  fp = fopen(RESULT, "at");
  fwrite(Answer3, 1, AnswerLength3, fp);
  fclose(fp);
  if (pid == NTHREAD - 1) {
    *FlagSavePtr = 4;
  } else {
    *FlagSavePtr = (pid + 1) * 5 + 3;
  }

  while (*FlagSavePtr != pid * 5 + 4) usleep(1);
  fp = fopen(RESULT, "at");
  fwrite(Answer4, 1, AnswerLength4, fp);
  fclose(fp);
  if (pid == NTHREAD - 1) {
    *FlagSavePtr = 5;
  } else {
    *FlagSavePtr = (pid + 1) * 5 + 4;
  }

#ifdef LOCAL
  if (pid == 0) {
    std::cerr << "TotalAnswer: " << *TotalAnswersPtr << "\n";
  }
#endif
}

void WaitFork() {
  while (true) {
    U32 x = 0;
    for (U32 i = 0; i < NTHREAD; ++i) x += FlagForkPtr[i];
    if (x == NTHREAD) {
      return;
    }
    usleep(1);
  }
}

int main() {
  std::cerr << std::fixed << std::setprecision(3);

  LoadData();

  pid_t Children[NTHREAD - 1] = {0};
  U32 pid = 0;
  for (U32 i = 0; i < NTHREAD - 1; i++) {
    if (pid == 0) {
      U32 x = fork();
      Children[i] = x;
      if (x == 0) {
        pid = i + 1;
        break;
      }
    }
  }

  GetShmPtr();
  FindCircle(pid);
  WaitFork();
  SaveAnswer(pid);

#ifdef XJBGJUDGE
  delete[] Answer0;
  delete[] Answer1;
  delete[] Answer2;
  delete[] Answer3;
  delete[] Answer4;
  delete[] ThreeCircle;
  delete[] Reachable;
#endif

  if (pid != 0) exit(0);
  exit(0);
}