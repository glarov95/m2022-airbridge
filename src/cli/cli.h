#ifndef M2022_CLI_H
#define M2022_CLI_H

/* Each command receives the arguments after its own name. */
int cmd_probe(int argc, char **argv);
int cmd_send(int argc, char **argv);
int cmd_decode(int argc, char **argv);
int cmd_server(int argc, char **argv);
int cmd_render(int argc, char **argv);
int cmd_encode(int argc, char **argv);
int cmd_install(int argc, char **argv);
int cmd_uninstall(int argc, char **argv);
int cmd_service(const char *verb, int argc, char **argv); /* start, stop, restart */
int cmd_status(int argc, char **argv);
int cmd_logs(int argc, char **argv);
int cmd_doctor(int argc, char **argv);

#endif /* M2022_CLI_H */
