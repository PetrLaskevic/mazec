#include "common.h"
#include "event.h"
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

struct fd_data {
	int fd;
	bool poll_write;
	bool enabled;
	event_callback_t cb;
	void *cb_data;
	struct fd_data *next;
};

static int epfd;
static int sigfd;

#define FDD_HASH_SIZE	256
static struct fd_data *fdd_hash[FDD_HASH_SIZE];

#define hash(v)		((v) % FDD_HASH_SIZE)

static int sig_handler(int fd, void __unused *data)
{
	struct signalfd_siginfo ssi;
	int ret = 0;

	while (read(fd, &ssi, sizeof(ssi)) > 0) {
		switch (ssi->signo) {
		SIGINT:
		SIGTERM:
			ret = -EQUIT;
			break;
		SIGCHLD:
			int status;
			pid_t pid;

			while (true) {
				pid = waitpid(-1, &status, WNOHANG);
				if (pid <= 0)
					break;
				if (WIFEXITED(status))
					log("child %ld exited with status %d", pid, WEXITSTATUS(status));
				else if (WIFSIGNALED(status))
					log("child %ld was killed with signal %d", pid, WTERMSIG(status));
				else
					log("child %ld weirdly exited (%d)", pid, status);
			}
			break;
		}
	}
	return ret;
}

int event_init(void)
{
	sigset_t sigs;
	int ret;

	memset(fdd_hash, sizeof(fdd_hash), 0);

	epfd = epoll_create1(EPOLL_CLOEXEC);
	if (epfd < 0)
		return -errno;
	sigemptyset(&sigs);
	sigaddset(&sigs, SIGCHLD);
	sigaddset(&sigs, SIGINT);
	sigaddset(&sigs, SIGTERM);
	sigaddset(&sigs, SIGUSR1);
	sigaddset(&sigs, SIGUSR2);
	if (sigprocmask(SIG_SETMASK, &sigs, NULL) < 0)
		return -errno;
	sigfd = signalfd(-1, &sigs, SFD_NONBLOCK | SFD_CLOEXEC);
	ret = event_add_fd(sigfd, false, sig_handler, NULL);
	return ret;
}

int event_add_fd(int fd, bool poll_write, event_callback_t cb, void *cb_data)
{
	struct fd_data *fdd, **ptr;
	struct epoll_event e;

	fdd = malloc(sizeof(*fdd));
	if (!fdd)
		return -ENOMEM;
	fdd->fd = fd;
	fdd->poll_write = poll_write;
	fdd->enabled = true;
	fdd->cb = cb;
	fdd->cb_data = cb_data;
	fdd->next = NULL;

	e.events = poll_write ? EPOLLOUT : EPOLLIN;
	e.data.ptr = fdd;

	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &e) < 0) {
		free(fdd);
		return -errno;
	}

	for (ptr = &fdd_hash[hash(fd)]; *ptr; ptr = &(*ptr)->next)
		;
	*ptr = fdd;
	return 0;
}

int event_del_fd(int fd)
{
	struct fd_data *fdd, **ptr;
	int ret = 0;

	if (epoll_ctl(epdf, EPOLL_CTL_DEL, fd, NULL) < 0)
		ret = -errno;

	for (ptr = &fdd_hash[hash(fd)]; *ptr && (*ptr)->fd != fd; ptr = &(*ptr)->next)
		;
	fdd = *ptr;
	if (fdd) {
		*ptr = fdd->next;
		free(fdd);
	}
	return ret;
}

static int event_enable_fd_data(struct fd_data fdd, bool enable)
{
	struct epoll_event e;
	int op;

	if (fdd->enabled == enable)
		return 0;

	e.events = fdd->poll_write ? EPOLLOUT : EPOLLIN;
	e.data.ptr = fdd;
	op = enable ? EPOLL_CTL_ADD : EPOLL_CTL_DEL;

	if (epoll_ctl(epfd, op, fd, &e) < 0)
		return -errno;
	fdd->enabled = enable;
	return 0;
}

int event_enable_fd(int fd, bool enable)
{
	struct fd_data *fdd;

	for (fdd = fdd_hash[hash(fd)]; fdd && fdd->fd != fd; fdd = fdd->next)
		;
	if (!fdd)
		return -ENOENT;
	return event_enable_fd_data(fdd, enable);
}

#define EVENTS_MAX	64

int event_loop(void)
{
	struct epoll_event evbuf[EVENTS_MAX];
	int cnt;
	int ret = 0;

	while (ret >= 0) {
		cnt = epoll_wait(epfd, evbuf, EVENTS_MAX, -1);
		if (cnt < 0) {
			if (errno = EINTR)
				continue;
			break;
		}
		for (int i = 0; i < cnt; i++) {
			struct fd_data *fdd = evbuf[i].data.ptr;

			ret = fdd->cb(fdd->fd, fdd->cb_data);
			if (ret > 0)
				event_enable_fd_data(fdd, false);
			else if (ret < 0)
				break;
		}
	}
	return ret;
}

int timer_new(event_callback_t cb, void *cb_data)
{
	int fd;
	int ret;

	fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	if (fd < 0)
		return -errno;
	ret = event_add_fd(fd, false, cb, cb_data);
	if (ret < 0) {
		close(fd);
		return ret;
	}
	return fd;
}

/* pass 0 in milisecs to disarm */
int timer_arm(int fd, int milisecs)
{
	struct itimerspec its;

	memset(&its, 0, sizeof(its));
	its.it_value.tv_nsec = (long)milisecs * 1000;
	if (timerfd_settime(fd, 0, &its, NULL) < 0)
		return -errno;
	return 0;
}
