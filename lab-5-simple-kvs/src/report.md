# Lab 5: Simple KVS — 보고서

- 이름: 이동호
- 학번: 2021-13385

## 1. 설계 개요

서버는 핸드아웃이 의도한 대로 세 개의 수정 가능 파일로 역할을 나누어
구현했다. `rwlock.c`는 동기화 프리미티브를 제공하고, `hashtable.c`는 그
위에 thread-safe 저장소를 쌓으며, `server.c`는 네트워크와 스레드 풀을
엮는다. `skvslib.c`가 요청 파싱과 해시테이블 API 디스패치를 이미
담당하므로, 내 역할은 이 계층들을 정확하고 동시성 안전하게 만드는
것이었다.

**`server.c`.** `main()`은 공유 listening 소켓 하나를 만들고
(`SO_REUSEADDR`, `0.0.0.0` 바인딩), `skvs_init()`을 호출한 뒤, 동일한
`listenfd`에서 함께 `accept()`하는 10개의 정적 워커 스레드 풀을 생성한다.
커널의 accept 큐가 연결 분배를 직렬화해 주므로 그 부분에는 별도 락이
필요 없다. 각 워커는 accept→세션 루프를 돈다. 세션 루프가 robust I/O의
핵심이다. `BUF_SIZE` 버퍼에 바이트를 누적하면서 매 `recv()`마다
`skvs_serve()`를 호출하고, 아직 요청이 미완성(개행 미수신)이면 계속
읽는다. 완전한 요청이 처리되면 `send_all()` 헬퍼로 응답을 돌려주는데, 이
헬퍼는 모든 바이트가 전송될 때까지 루프를 돌며 짧은 쓰기와
`EINTR`/`EAGAIN`을 견딘다. 프로토콜이 half-duplex이므로 요청 처리 후
버퍼를 비운다. `recv`가 0을 반환(클라이언트가 빈 줄/EOF로 종료)하면
세션을 끝내고 워커는 다시 `accept()`로 돌아간다.

**`hashtable.c`.** 네 연산 모두 키를 버킷으로 해싱하여 그 버킷의 개별
락을 잡는다 — fine-grained locking이므로 서로 다른 버킷에 해당하는 키
연산은 완전히 병렬로 진행된다. 읽기는 read lock(quick을 그대로 전달),
insert/update/delete는 write lock을 잡는다. `hash_read`는 헤더 요구대로
read lock을 풀기 *전에* 값을 호출자 버퍼로 복사하므로, 동시 writer가 복사
도중 문자열을 free할 수 없다. 메모리 소유권도 명확히 했다. insert는 키와
값을 모두 `strdup`하고, update는 새 값을 `strdup`해 성공한 뒤에만 옛 값을
free하며, delete는 노드를 언링크한 뒤 키·값·노드를 free한다.

**`rwlock.c`.** 커스텀 락은 `pthread_rwlock`을 전혀 쓰지 않고
`pthread_mutex_t` 하나와 `pthread_cond_t` 하나로 구성하며, 상태는 힙에
할당한 `struct uctx`에 둔다.

## 2. 동시성 전략

이 락은 **FIFO 순서**와 **quick read 선점**을 티켓 스킴으로 결합한다. 두
카운터가 핵심이다. `next_ticket`(non-quick 호출자마다 발급)과
`now_serving`(현재 진행이 허용된 티켓). non-quick 호출자는 `now_serving`이
자기 티켓에 도달할 때까지 condition variable에서 대기한다.

핵심은 turn을 *언제* 넘기느냐의 비대칭성이다. **reader**는 자기 차례가
되면 `current_readers`를 증가시키고 *즉시* `now_serving`을 전진시킨 뒤
broadcast한다. 이로써 큐에 연속으로 쌓인 reader들이 연쇄적으로 깨어나
동시에 실행된다 — 슬라이드의 "concurrent" 그룹이다. 반면 **writer**는
임계구역 전체 동안 `now_serving`을 자기 티켓에 붙잡아 두고
`rwlock_write_unlock`에서만 전진시킨다. 그래서 뒤의 모든 요청이 막히고
writer는 엄격히 순차 처리된다. 모든 티켓이 `now_serving`을 정확히 한 번씩
전진시키므로(reader는 lock 시점, writer는 unlock 시점) 시퀀스가
일관되고 누구도 건너뛰지 않는다 — 이것이 writer starvation을 막는다.

**quick read**는 일부러 티켓 시퀀스 *바깥*에 위치한다. 티켓을 받지 않고
`current_writers == 0`만 기다린 뒤 `current_readers`를 올리고 실행한다.
따라서 대기 중인 모든 FIFO writer/reader를 앞질러 가지만(선점), *활성*
writer는 존중하여 그것이 끝날 때까지 기다린다. quick read와 FIFO reader가
같은 순간(예: writer가 막 락을 풀 때)에 실행 가능해지면 둘 다 단순히
reader가 되어 함께 돈다. 이는 quick read 테스트에서 관찰된 동작과 정확히
일치한다(QREAD가, 아직 대기 중인 UPDATE/DELETE보다 앞서 있는 FIFO READ와
같은 시점에 값을 반환).

unlock 함수 상단의 `sleep(rw->delay)`는 내부 mutex를 잡기 전, 그리고
reader/writer 카운터를 감소시키기 전에 실행되므로 sleep 동안 락이 실제로
유지된다 — 이것이 `-d 5` semantic 테스트를 관찰 가능하게 만든다.

## 3. 비자명한 점

- **Graceful shutdown.** `accept()`와 `recv()` 모두 1초 `SO_RCVTIMEO`를
  사용하므로, 둘 중 하나에서 블록된 워커가 적어도 1초에 한 번 깨어나
  SIGINT 핸들러가 세운 `g_shutdown` 플래그를 다시 확인한다. `main()`은 모든
  워커를 join한 *뒤에야* `skvs_destroy(ctx, 1)`을 호출하여, 모든 워커가
  테이블 접근을 멈춘 후 해시테이블 덤프가 정확히 한 번 실행되게 한다.
- **SIGPIPE**는 무시하고 `send()`는 `MSG_NOSIGNAL`을 쓰므로, 응답 도중
  접속을 끊는 클라이언트가 서버를 죽일 수 없다. SIGINT 핸들러는
  async-signal-safe하도록 `errno`를 저장/복원한다.
- 모든 깨우기에 targeted signal 대신 단일 broadcast를 쓴다. 대기자가 최대
  10개이므로 spurious wakeup 비용이 미미하고, 각 대기자가 자기 술어를 다시
  검사하므로 정확성을 논증하기 쉽다.

## 4. AI 사용 명시

이 과제에 AI 어시스턴트(Claude)를 사용했다. `rwlock.c`의 티켓 기반 FIFO +
quick read 스킴 설계, `rwlock.c`/`hashtable.c`/`server.c`의 초기 구현 및 본
보고서 초안 작성을 도왔고, 락 의미·참조 클라이언트와의 상호운용성·누수 및
race 부재를 검증하는 테스트 하니스(`rwtest.sh`의 `ts` 비의존 재구현,
동시성 스트레스 테스트, valgrind/ThreadSanitizer 실행)를 구성하는 데도
사용했다. 나는 참조 바이너리와 다섯 가지 self-test 시나리오에 대해 모든
부분을 직접 실행·검증했으며, 제출한 코드를 이해하고 방어할 책임이 전적으로
나에게 있음을 분명히 한다.
