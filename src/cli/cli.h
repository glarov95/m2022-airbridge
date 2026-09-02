#ifndef M2022_CLI_H
#define M2022_CLI_H

/* Each command receives the arguments after its own name. */
int cmd_probe(int argc, char **argv);
int cmd_send(int argc, char **argv);
int cmd_decode(int argc, char **argv);
int cmd_server(int argc, char **argv);
int cmd_render(int argc, char **argv);

#endif /* M2022_CLI_H */
