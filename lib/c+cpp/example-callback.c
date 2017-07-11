#include <maze.h>
#include <stdio.h>
#include <stdlib.h>

#define USER	"karel"
#define LEVEL	"bagr"

#define MAZE_SERVER_NAME	"localhost"
#define MAZE_SERVER_PORT	"4000"

// Jdi stále vzhůru.
static char callback(maze_t *m, const char *nope) {
  return 'W';
}

int main(void) {
  maze_t *m = maze_run(MAZE_SERVER_NAME, MAZE_SERVER_PORT, USER, LEVEL, NULL, callback);
  if (m)
    maze_close(m);

  return 0;
}
