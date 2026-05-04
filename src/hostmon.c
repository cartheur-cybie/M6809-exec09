#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <glob.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <ctype.h>

#include "hostmon.h"

#define HOSTMON_MAX_PORTS 256
#define HOSTMON_MAX_HEX   4096
#define HOSTMON_MAX_PROFILES 32

struct hostmon_profile
{
	char name[64];
	char port[256];
	int baud;
};

static struct hostmon_profile g_profiles[HOSTMON_MAX_PROFILES];
static int g_profile_count = 0;
static char g_default_profile[64] = "VOLATCO-A";
static const char *g_config_path = "monitor-profiles.conf";
static int g_config_loaded = 0;

static void usage (void)
{
	printf ("exec09-monitor usage:\n");
	printf ("  exec09-monitor [--config FILE] shell\n");
	printf ("  exec09-monitor shell\n");
	printf ("  exec09-monitor ports\n");
	printf ("  exec09-monitor profiles\n");
	printf ("  exec09-monitor diag [device ...] [--baud N]\n");
	printf ("  exec09-monitor connect <device> [--baud N]\n");
	printf ("  exec09-monitor loopback <device> [hex-bytes] [--wait-ms N] [--baud N]\n");
	printf ("  exec09-monitor link-test <tx-device> <rx-device> [hex-bytes] [--wait-ms N] [--baud N]\n");
	printf ("  exec09-monitor transceive <device> <hex-bytes> <read-len> [--wait-ms N] [--baud N]\n");
	printf ("  exec09-monitor plug <device> [hex-bytes] [read-len] [wait-ms] [--baud N]\n");
	printf ("aliases: VOLATCO-A VOLATCO-B (resolved via /dev/serial/by-id)\n");
}

static int run_cmd (int argc, char **argv);
static int read_some_wait (int fd, unsigned char *buf, int maxlen, int wait_ms);
static void print_hex (const unsigned char *buf, int len);
static int load_profiles_config (const char *path);
static const struct hostmon_profile *find_profile (const char *name);

static speed_t baud_to_speed (int baud)
{
	switch (baud)
	{
		case 1200: return B1200;
		case 2400: return B2400;
		case 4800: return B4800;
		case 9600: return B9600;
		case 19200: return B19200;
		case 38400: return B38400;
#ifdef B57600
		case 57600: return B57600;
#endif
#ifdef B115200
		case 115200: return B115200;
#endif
#ifdef B230400
		case 230400: return B230400;
#endif
		default: return 0;
	}
}

static int setup_tty (int fd, int baud)
{
	struct termios tio;
	speed_t speed = baud_to_speed (baud);
	if (!speed)
		return -1;
	if (tcgetattr (fd, &tio) != 0)
		return -1;
	cfmakeraw (&tio);
	tio.c_cflag |= CLOCAL | CREAD;
#ifdef CRTSCTS
	tio.c_cflag &= ~CRTSCTS;
#endif
	if (cfsetispeed (&tio, speed) != 0)
		return -1;
	if (cfsetospeed (&tio, speed) != 0)
		return -1;
	tio.c_cc[VMIN] = 0;
	tio.c_cc[VTIME] = 0;
	if (tcsetattr (fd, TCSANOW, &tio) != 0)
		return -1;
	return 0;
}

static int open_serial (const char *dev, int baud)
{
	int fd = open (dev, O_RDWR | O_NOCTTY | O_SYNC);
	if (fd < 0)
		return -1;
	if (setup_tty (fd, baud) != 0)
	{
		close (fd);
		errno = EINVAL;
		return -1;
	}
	return fd;
}

static int write_all (int fd, const unsigned char *buf, int len)
{
	int off = 0;
	while (off < len)
	{
		int rc = write (fd, buf + off, len - off);
		if (rc < 0)
		{
			if (errno == EINTR)
				continue;
			return -1;
		}
		off += rc;
	}
	return off;
}

static int transceive_on_fd (int fd, const unsigned char *tx, int txlen, int readlen, int wait_ms, int compare_loopback)
{
	unsigned char rx[HOSTMON_MAX_HEX];
	int got;
	tcflush (fd, TCIOFLUSH);
	if (write_all (fd, tx, txlen) < 0)
	{
		fprintf (stderr, "write FAIL: %s\n", strerror (errno));
		return 4;
	}
	if (wait_ms > 0)
		usleep ((unsigned long)wait_ms * 1000UL);
	got = read_some_wait (fd, rx, readlen, wait_ms > 0 ? wait_ms : 100);
	if (got < 0)
	{
		fprintf (stderr, "read FAIL: %s\n", strerror (errno));
		return 5;
	}
	printf ("tx (%d): ", txlen);
	print_hex (tx, txlen);
	printf ("rx (%d): ", got);
	print_hex (rx, got);
	if (compare_loopback)
	{
		if ((got != txlen) || memcmp (tx, rx, txlen))
		{
			printf ("loopback: FAIL\n");
			return 6;
		}
		printf ("loopback: PASS\n");
	}
	return 0;
}

static int read_some_wait (int fd, unsigned char *buf, int maxlen, int wait_ms)
{
	fd_set rfds;
	struct timeval tv;
	int total = 0;
	while (total < maxlen)
	{
		FD_ZERO (&rfds);
		FD_SET (fd, &rfds);
		tv.tv_sec = wait_ms / 1000;
		tv.tv_usec = (wait_ms % 1000) * 1000;
		int rc = select (fd + 1, &rfds, NULL, NULL, &tv);
		if (rc < 0)
		{
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (rc == 0)
			break;
		rc = read (fd, buf + total, maxlen - total);
		if (rc < 0)
		{
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (rc == 0)
			break;
		total += rc;
	}
	return total;
}

static void print_hex (const unsigned char *buf, int len)
{
	int i;
	for (i = 0; i < len; i++)
	{
		printf ("%02X", buf[i]);
		if (i + 1 < len)
			putchar (' ');
	}
	putchar ('\n');
}

static int parse_hex_bytes (const char *s, unsigned char *out, int cap)
{
	char clean[HOSTMON_MAX_HEX * 2 + 1];
	int ci = 0;
	int i;
	for (i = 0; s[i] != '\0'; i++)
	{
		char c = s[i];
		if (c == ' ' || c == '\t' || c == '-' || c == ':' || c == ',')
			continue;
		if (c == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X'))
		{
			i++;
			continue;
		}
		if (((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F')))
		{
			if (ci >= (int)sizeof (clean) - 1)
				return -1;
			clean[ci++] = c;
		}
		else
		{
			return -1;
		}
	}
	if ((ci == 0) || (ci & 1))
		return -1;
	clean[ci] = '\0';
	if (ci / 2 > cap)
		return -1;
	for (i = 0; i < ci / 2; i++)
	{
		char byte_s[3];
		byte_s[0] = clean[i * 2];
		byte_s[1] = clean[i * 2 + 1];
		byte_s[2] = '\0';
		out[i] = (unsigned char)strtoul (byte_s, NULL, 16);
	}
	return ci / 2;
}

static int append_unique (char ports[][256], int *count, const char *s)
{
	int i;
	for (i = 0; i < *count; i++)
		if (!strcmp (ports[i], s))
			return 0;
	if (*count >= HOSTMON_MAX_PORTS)
		return -1;
	strncpy (ports[*count], s, 255);
	ports[*count][255] = '\0';
	(*count)++;
	return 0;
}

static int gather_ports (char ports[][256], int *count)
{
	glob_t g;
	int i;
	*count = 0;
	if (glob ("/dev/serial/by-id/*", 0, NULL, &g) == 0)
	{
		for (i = 0; i < (int)g.gl_pathc; i++)
			append_unique (ports, count, g.gl_pathv[i]);
		globfree (&g);
	}
	if (glob ("/dev/ttyUSB*", 0, NULL, &g) == 0)
	{
		for (i = 0; i < (int)g.gl_pathc; i++)
			append_unique (ports, count, g.gl_pathv[i]);
		globfree (&g);
	}
	if (glob ("/dev/ttyACM*", 0, NULL, &g) == 0)
	{
		for (i = 0; i < (int)g.gl_pathc; i++)
			append_unique (ports, count, g.gl_pathv[i]);
		globfree (&g);
	}
	return *count;
}

static const char *resolve_alias (const char *name, char *out, size_t out_sz)
{
	glob_t g;
	int i;
	const char *needle = NULL;

	if (!strcmp (name, "VOLATCO-A") || !strcmp (name, "A"))
		needle = "VOL01-00A";
	else if (!strcmp (name, "VOLATCO-B") || !strcmp (name, "B"))
		needle = "VOL01-00B";
	else
		return name;

	if (glob ("/dev/serial/by-id/*", 0, NULL, &g) == 0)
	{
		for (i = 0; i < (int)g.gl_pathc; i++)
		{
			if (strstr (g.gl_pathv[i], needle))
			{
				strncpy (out, g.gl_pathv[i], out_sz - 1);
				out[out_sz - 1] = '\0';
				globfree (&g);
				return out;
			}
		}
		globfree (&g);
	}

	/* fallback if by-id is unavailable */
	if (!strcmp (needle, "VOL01-00A"))
		return "/dev/ttyUSB0";
	return "/dev/ttyUSB1";
}

static char *trim_ws (char *s)
{
	char *e;
	while (*s && isspace ((unsigned char)*s))
		s++;
	if (*s == '\0')
		return s;
	e = s + strlen (s) - 1;
	while (e >= s && isspace ((unsigned char)*e))
		*e-- = '\0';
	return s;
}

static const struct hostmon_profile *find_profile (const char *name)
{
	int i;
	for (i = 0; i < g_profile_count; i++)
		if (!strcmp (g_profiles[i].name, name))
			return &g_profiles[i];
	return NULL;
}

static const char *resolve_target (const char *name_or_device, char *out, size_t out_sz, int *baud_out)
{
	const struct hostmon_profile *p = find_profile (name_or_device);
	if (p)
	{
		if (baud_out && p->baud > 0)
			*baud_out = p->baud;
		strncpy (out, p->port, out_sz - 1);
		out[out_sz - 1] = '\0';
		return out;
	}
	return resolve_alias (name_or_device, out, out_sz);
}

static int load_profiles_config (const char *path)
{
	FILE *fp;
	char line[1024];
	char current_section[64] = "";
	struct hostmon_profile *current = NULL;
	int lineno = 0;

	g_profile_count = 0;
	strncpy (g_default_profile, "VOLATCO-A", sizeof (g_default_profile) - 1);
	g_default_profile[sizeof (g_default_profile) - 1] = '\0';

	fp = fopen (path, "r");
	if (!fp)
		return -1;
	while (fgets (line, sizeof (line), fp))
	{
		char *s;
		char *eq;
		lineno++;
		line[strcspn (line, "\r\n")] = '\0';
		s = trim_ws (line);
		if (*s == '\0' || *s == '#' || *s == ';')
			continue;
		if (*s == '[')
		{
			char *end = strchr (s, ']');
			if (!end)
				continue;
			*end = '\0';
			strncpy (current_section, trim_ws (s + 1), sizeof (current_section) - 1);
			current_section[sizeof (current_section) - 1] = '\0';
			if (g_profile_count < HOSTMON_MAX_PROFILES)
			{
				current = &g_profiles[g_profile_count++];
				memset (current, 0, sizeof (*current));
				strncpy (current->name, current_section, sizeof (current->name) - 1);
				current->baud = 0;
			}
			else
			{
				current = NULL;
			}
			continue;
		}
		eq = strchr (s, '=');
		if (!eq)
			continue;
		*eq = '\0';
		{
			char *k = trim_ws (s);
			char *v = trim_ws (eq + 1);
			if (*current_section == '\0')
			{
				if (!strcmp (k, "default"))
				{
					strncpy (g_default_profile, v, sizeof (g_default_profile) - 1);
					g_default_profile[sizeof (g_default_profile) - 1] = '\0';
				}
			}
			else if (current)
			{
				if (!strcmp (k, "port"))
				{
					strncpy (current->port, v, sizeof (current->port) - 1);
					current->port[sizeof (current->port) - 1] = '\0';
				}
				else if (!strcmp (k, "baud"))
				{
					current->baud = atoi (v);
				}
			}
		}
		(void)lineno;
	}
	fclose (fp);
	return 0;
}

static int cmd_ports (void)
{
	char ports[HOSTMON_MAX_PORTS][256];
	int count, i;
	gather_ports (ports, &count);
	printf ("ports: %d\n", count);
	for (i = 0; i < count; i++)
		printf ("%s\n", ports[i]);
	return 0;
}

static int cmd_profiles (void)
{
	int i;
	if (g_profile_count > 0)
	{
		printf ("profiles from %s (default=%s)\n", g_config_path, g_default_profile);
		for (i = 0; i < g_profile_count; i++)
		{
			printf ("%s -> %s", g_profiles[i].name, g_profiles[i].port[0] ? g_profiles[i].port : "(unset)");
			if (g_profiles[i].baud > 0)
				printf (" @ %d", g_profiles[i].baud);
			printf ("\n");
		}
	}
	else
	{
		char buf[256];
		printf ("VOLATCO-A -> %s\n", resolve_alias ("VOLATCO-A", buf, sizeof (buf)));
		printf ("VOLATCO-B -> %s\n", resolve_alias ("VOLATCO-B", buf, sizeof (buf)));
	}
	return 0;
}

static int probe_device (const char *dev, int baud)
{
	int fd = open_serial (dev, baud);
	if (fd < 0)
	{
		printf ("open FAIL: %s (%s)\n", dev, strerror (errno));
		return 1;
	}
	close (fd);
	printf ("open OK: %s @ %d\n", dev, baud);
	return 0;
}

static int cmd_diag (int argc, char **argv)
{
	int baud = 921600;
	int i;
	int failures = 0;
	int explicit_count = 0;
	for (i = 2; i < argc; i++)
	{
		if (!strcmp (argv[i], "--baud") && i + 1 < argc)
		{
			baud = atoi (argv[++i]);
			continue;
		}
		explicit_count++;
	}

	if (explicit_count == 0)
	{
		char ports[HOSTMON_MAX_PORTS][256];
		int count = 0;
		gather_ports (ports, &count);
		printf ("diag: %d candidate ports\n", count);
		for (i = 0; i < count; i++)
			failures += probe_device (ports[i], baud);
	}
	else
	{
		for (i = 2; i < argc; i++)
		{
			if (!strcmp (argv[i], "--baud"))
			{
				i++;
				continue;
			}
			failures += probe_device (argv[i], baud);
		}
	}
	return failures ? 3 : 0;
}

static int cmd_connect (int argc, char **argv)
{
	int baud = 921600;
	const char *dev;
	char resolved[256];
	int fd;
	int i;
	if (argc < 3)
		return 1;
	dev = resolve_target (argv[2], resolved, sizeof (resolved), &baud);
	for (i = 3; i < argc; i++)
		if (!strcmp (argv[i], "--baud") && i + 1 < argc)
			baud = atoi (argv[++i]);
	fd = open_serial (dev, baud);
	if (fd < 0)
	{
		fprintf (stderr, "connect FAIL: %s (%s)\n", dev, strerror (errno));
		return 3;
	}
	printf ("connect OK: %s @ %d\n", dev, baud);
	close (fd);
	return 0;
}

static int cmd_transceive_like (const char *dev, const unsigned char *tx, int txlen, int readlen, int wait_ms, int baud, int compare_loopback)
{
	int fd;
	fd = open_serial (dev, baud);
	if (fd < 0)
	{
		fprintf (stderr, "open FAIL: %s (%s)\n", dev, strerror (errno));
		return 3;
	}
	{
		int rc = transceive_on_fd (fd, tx, txlen, readlen, wait_ms, compare_loopback);
		close (fd);
		return rc;
	}
}

static int cmd_link_test (int argc, char **argv)
{
	unsigned char tx[HOSTMON_MAX_HEX];
	unsigned char rx[HOSTMON_MAX_HEX];
	int txlen = 4;
	int wait_ms = 80;
	int baud = 921600;
	int txfd, rxfd, got, i;
	const char *txdev;
	const char *rxdev;
	char txresolved[256], rxresolved[256];

	if (argc < 4)
		return 1;
	txdev = resolve_target (argv[2], txresolved, sizeof (txresolved), &baud);
	rxdev = resolve_target (argv[3], rxresolved, sizeof (rxresolved), &baud);
	tx[0] = 0xA5; tx[1] = 0x5A; tx[2] = 0x00; tx[3] = 0xFF;

	if (argc >= 5 && strncmp (argv[4], "--", 2))
	{
		txlen = parse_hex_bytes (argv[4], tx, HOSTMON_MAX_HEX);
		if (txlen < 0)
			return 2;
	}
	for (i = 4; i < argc; i++)
	{
		if (!strcmp (argv[i], "--wait-ms") && i + 1 < argc)
			wait_ms = atoi (argv[++i]);
		else if (!strcmp (argv[i], "--baud") && i + 1 < argc)
			baud = atoi (argv[++i]);
	}

	txfd = open_serial (txdev, baud);
	if (txfd < 0)
	{
		fprintf (stderr, "open tx FAIL: %s (%s)\n", txdev, strerror (errno));
		return 3;
	}
	rxfd = open_serial (rxdev, baud);
	if (rxfd < 0)
	{
		fprintf (stderr, "open rx FAIL: %s (%s)\n", rxdev, strerror (errno));
		close (txfd);
		return 4;
	}

	tcflush (txfd, TCIOFLUSH);
	tcflush (rxfd, TCIOFLUSH);
	if (write_all (txfd, tx, txlen) < 0)
	{
		fprintf (stderr, "write FAIL: %s\n", strerror (errno));
		close (txfd);
		close (rxfd);
		return 5;
	}
	if (wait_ms > 0)
		usleep ((unsigned long)wait_ms * 1000UL);
	got = read_some_wait (rxfd, rx, txlen, wait_ms > 0 ? wait_ms : 100);
	if (got < 0)
	{
		fprintf (stderr, "read FAIL: %s\n", strerror (errno));
		close (txfd);
		close (rxfd);
		return 6;
	}
	printf ("tx-dev: %s\n", txdev);
	printf ("rx-dev: %s\n", rxdev);
	printf ("tx (%d): ", txlen); print_hex (tx, txlen);
	printf ("rx (%d): ", got); print_hex (rx, got);
	close (txfd);
	close (rxfd);
	if ((got != txlen) || memcmp (tx, rx, txlen))
	{
		printf ("link-test: FAIL\n");
		return 7;
	}
	printf ("link-test: PASS\n");
	return 0;
}

static int cmd_loopback (int argc, char **argv)
{
	unsigned char tx[HOSTMON_MAX_HEX];
	int txlen = 4;
	int wait_ms = 80;
	int baud = 921600;
	const char *dev;
	char resolved[256];
	int i;
	tx[0] = 0xA5; tx[1] = 0x5A; tx[2] = 0x00; tx[3] = 0xFF;
	if (argc < 3)
		return 1;
	dev = resolve_target (argv[2], resolved, sizeof (resolved), &baud);
	if (argc >= 4 && strncmp (argv[3], "--", 2))
	{
		txlen = parse_hex_bytes (argv[3], tx, HOSTMON_MAX_HEX);
		if (txlen < 0)
			return 2;
	}
	for (i = 3; i < argc; i++)
	{
		if (!strcmp (argv[i], "--wait-ms") && i + 1 < argc)
			wait_ms = atoi (argv[++i]);
		else if (!strcmp (argv[i], "--baud") && i + 1 < argc)
			baud = atoi (argv[++i]);
	}
	return cmd_transceive_like (dev, tx, txlen, txlen, wait_ms, baud, 1);
}

static int cmd_transceive (int argc, char **argv)
{
	unsigned char tx[HOSTMON_MAX_HEX];
	int txlen;
	int readlen;
	int wait_ms = 80;
	int baud = 921600;
	const char *dev;
	char resolved[256];
	int i;
	if (argc < 5)
		return 1;
	dev = resolve_target (argv[2], resolved, sizeof (resolved), &baud);
	txlen = parse_hex_bytes (argv[3], tx, HOSTMON_MAX_HEX);
	if (txlen < 0)
		return 2;
	readlen = atoi (argv[4]);
	for (i = 5; i < argc; i++)
	{
		if (!strcmp (argv[i], "--wait-ms") && i + 1 < argc)
			wait_ms = atoi (argv[++i]);
		else if (!strcmp (argv[i], "--baud") && i + 1 < argc)
			baud = atoi (argv[++i]);
	}
	return cmd_transceive_like (dev, tx, txlen, readlen, wait_ms, baud, 0);
}

static int cmd_plug (int argc, char **argv)
{
	char *fake_argv[16];
	int fake_argc = 0;
	/* plug maps to transceive defaults: probe 00, read 16, wait 50 */
	fake_argv[fake_argc++] = argv[0];
	fake_argv[fake_argc++] = "transceive";
	if (argc < 3)
		return 1;
	fake_argv[fake_argc++] = argv[2];
	if (argc >= 4 && strncmp (argv[3], "--", 2))
		fake_argv[fake_argc++] = argv[3];
	else
		fake_argv[fake_argc++] = "00";
	if (argc >= 5 && strncmp (argv[4], "--", 2))
		fake_argv[fake_argc++] = argv[4];
	else
		fake_argv[fake_argc++] = "16";
	if (argc >= 6 && strncmp (argv[5], "--", 2))
		fake_argv[fake_argc++] = argv[5];
	else
	{
		fake_argv[fake_argc++] = "--wait-ms";
		fake_argv[fake_argc++] = "50";
	}
	while (fake_argc < 16 && (fake_argc - 2 + 3) < argc)
		fake_argv[fake_argc++] = argv[fake_argc - 2 + 3];
	return cmd_transceive (fake_argc, fake_argv);
}

static void str_upper_inplace (char *s)
{
	int i;
	for (i = 0; s[i] != '\0'; i++)
		s[i] = (char)toupper ((unsigned char)s[i]);
}

static int split_words (char *line, char **outv, int cap)
{
	int n = 0;
	char *tok = strtok (line, " \t\r\n");
	while (tok && n < cap)
	{
		outv[n++] = tok;
		tok = strtok (NULL, " \t\r\n");
	}
	return n;
}

static int handle_aforth_phrase (const char *line_up, const char *profile, int baud, int *af_loaded, int connected_fd)
{
	char *argv[12];
	int argc = 0;
	char prog[] = "exec09-monitor";
	char cmd_trans[] = "transceive";
	char wait_opt[] = "--wait-ms";
	char wait_val[] = "50";
	char baud_opt[] = "--baud";
	char baud_val[32];

	if (!strcmp (line_up, "HI"))
	{
		printf ("AFORTH mode ready. profile=%s loaded=%s\n", profile, *af_loaded ? "yes" : "no");
		return 0;
	}
	if (!strcmp (line_up, "AFORTH"))
	{
		*af_loaded = 1;
		printf ("AFORTH loaded\n");
		return 0;
	}
	if (!strcmp (line_up, "HOST LOAD"))
	{
		if (!*af_loaded)
		{
			printf ("HOST LOAD requires AFORTH\n");
			return 0;
		}
		if (connected_fd >= 0)
		{
			unsigned char tx[1] = {0x00};
			printf ("using existing connection for %s\n", profile);
			return transceive_on_fd (connected_fd, tx, 1, 16, 50, 0);
		}
		snprintf (baud_val, sizeof (baud_val), "%d", baud);
		argv[argc++] = prog;
		argv[argc++] = cmd_trans;
		argv[argc++] = (char *)profile;
		argv[argc++] = "00";
		argv[argc++] = "16";
		argv[argc++] = wait_opt;
		argv[argc++] = wait_val;
		argv[argc++] = baud_opt;
		argv[argc++] = baud_val;
		return run_cmd (argc, argv);
	}
	if (!strcmp (line_up, "SERIAL LOAD"))
	{
		if (!*af_loaded)
		{
			printf ("SERIAL LOAD requires AFORTH\n");
			return 0;
		}
		if (connected_fd >= 0)
		{
			unsigned char tx[1] = {0x00};
			printf ("using existing connection for %s\n", profile);
			return transceive_on_fd (connected_fd, tx, 1, 16, 80, 0);
		}
		snprintf (baud_val, sizeof (baud_val), "%d", baud);
		argv[argc++] = prog;
		argv[argc++] = cmd_trans;
		argv[argc++] = (char *)profile;
		argv[argc++] = "00";
		argv[argc++] = "16";
		argv[argc++] = baud_opt;
		argv[argc++] = baud_val;
		return run_cmd (argc, argv);
	}
	if (!strcmp (line_up, "TALK") || !strcmp (line_up, "DRIVE HI"))
	{
		printf ("%s queued for profile %s\n", line_up, profile);
		return 0;
	}
	return -1;
}

static int cmd_shell (void)
{
	char line[1024];
	char line_copy[1024];
	char line_up[1024];
	char *argv[32];
	char *argv2[33];
	int argc;
	int argc2;
	int af_mode = 0;
	int af_loaded = 0;
	int baud = 921600;
	char profile[64];
	int fd_a = -1;
	int fd_b = -1;

	strncpy (profile, g_default_profile, sizeof (profile) - 1);
	profile[sizeof (profile) - 1] = '\0';

	printf ("monitor shell. type 'exit' to quit.\n");
	while (1)
	{
		printf (af_mode ? "aforth> " : "monitor> ");
		fflush (stdout);
		if (!fgets (line, sizeof (line), stdin))
			break;
		strncpy (line_copy, line, sizeof (line_copy) - 1);
		line_copy[sizeof (line_copy) - 1] = '\0';
		argc = split_words (line_copy, argv, 32);
		if (argc == 0)
			continue;

		strncpy (line_up, line, sizeof (line_up) - 1);
		line_up[sizeof (line_up) - 1] = '\0';
		line_up[strcspn (line_up, "\r\n")] = '\0';
		str_upper_inplace (line_up);

		if (!strcmp (line_up, "EXIT") || !strcmp (line_up, "QUIT"))
			return 0;
		if (!strcmp (line_up, "AFORTH") && !af_mode)
		{
			af_mode = 1;
			printf ("entered AFORTH mode\n");
			continue;
		}
		if (!strcmp (line_up, "MONITOR"))
		{
			af_mode = 0;
			printf ("returned to monitor mode\n");
			continue;
		}
		if (!strcmp (line_up, "A") || !strcmp (line_up, "VOLATCO-A"))
		{
			strncpy (profile, "VOLATCO-A", sizeof (profile) - 1);
			profile[sizeof (profile) - 1] = '\0';
			printf ("profile: %s\n", profile);
			continue;
		}
		if (!strcmp (line_up, "B") || !strcmp (line_up, "VOLATCO-B"))
		{
			strncpy (profile, "VOLATCO-B", sizeof (profile) - 1);
			profile[sizeof (profile) - 1] = '\0';
			printf ("profile: %s\n", profile);
			continue;
		}
		if (!strcmp (argv[0], "profile") && argc >= 2)
		{
			strncpy (profile, argv[1], sizeof (profile) - 1);
			profile[sizeof (profile) - 1] = '\0';
			printf ("profile: %s\n", profile);
			continue;
		}
		if (!strncmp (line_up, "BAUD ", 5))
		{
			int new_baud = (argc >= 2) ? atoi (argv[1]) : 0;
			if (new_baud > 0)
			{
				baud = new_baud;
				printf ("baud: %d\n", baud);
			}
			continue;
		}
		if (!strcmp (line_up, "CONNECT"))
		{
			char resolved[256];
			int profile_baud = baud;
			const char *dev = resolve_target (profile, resolved, sizeof (resolved), &profile_baud);
			int *slot = !strcmp (profile, "VOLATCO-B") || !strcmp (profile, "B") ? &fd_b : &fd_a;
			if (*slot >= 0)
				close (*slot);
			*slot = open_serial (dev, profile_baud);
			if (*slot < 0)
				fprintf (stderr, "connect FAIL: %s (%s)\n", dev, strerror (errno));
			else
				printf ("connect OK (persistent): %s @ %d\n", dev, profile_baud);
			continue;
		}
		if (!strcmp (line_up, "DISCONNECT"))
		{
			int *slot = !strcmp (profile, "VOLATCO-B") || !strcmp (profile, "B") ? &fd_b : &fd_a;
			if (*slot >= 0)
			{
				close (*slot);
				*slot = -1;
				printf ("persistent connection closed for profile %s\n", profile);
			}
			else
			{
				printf ("profile %s already disconnected\n", profile);
			}
			continue;
		}
		if (!strcmp (line_up, "DISCONNECT ALL"))
		{
			if (fd_a >= 0) { close (fd_a); fd_a = -1; }
			if (fd_b >= 0) { close (fd_b); fd_b = -1; }
			printf ("all persistent connections closed\n");
			continue;
		}
		if (!strcmp (line_up, "STATUS"))
		{
			printf ("A: %s\n", fd_a >= 0 ? "connected" : "disconnected");
			printf ("B: %s\n", fd_b >= 0 ? "connected" : "disconnected");
			printf ("active profile: %s baud=%d\n", profile, baud);
			continue;
		}

		if (af_mode)
		{
			int current_fd = !strcmp (profile, "VOLATCO-B") || !strcmp (profile, "B") ? fd_b : fd_a;
			int af_rc = handle_aforth_phrase (line_up, profile, baud, &af_loaded, current_fd);
			if (af_rc >= 0)
				continue;
		}

		/* pass through to normal monitor commands in either mode */
		argc2 = 0;
		argv2[argc2++] = "exec09-monitor";
		{
			int i;
			for (i = 0; i < argc && argc2 < 32; i++)
				argv2[argc2++] = argv[i];
		}
		argv2[argc2] = NULL;
		run_cmd (argc2, argv2);
	}
	if (fd_a >= 0)
		close (fd_a);
	if (fd_b >= 0)
		close (fd_b);
	return 0;
}

static int run_cmd (int argc, char **argv)
{
	const char *cmd;
	if (argc < 2)
		return 1;
	cmd = argv[1];
	if (!strcmp (cmd, "shell"))
		return cmd_shell ();
	if (!strcmp (cmd, "ports"))
		return cmd_ports ();
	if (!strcmp (cmd, "profiles"))
		return cmd_profiles ();
	if (!strcmp (cmd, "diag"))
		return cmd_diag (argc, argv);
	if (!strcmp (cmd, "connect"))
		return cmd_connect (argc, argv);
	if (!strcmp (cmd, "loopback"))
		return cmd_loopback (argc, argv);
	if (!strcmp (cmd, "link-test"))
		return cmd_link_test (argc, argv);
	if (!strcmp (cmd, "transceive"))
		return cmd_transceive (argc, argv);
	if (!strcmp (cmd, "plug"))
		return cmd_plug (argc, argv);
	if (!strcmp (cmd, "help"))
	{
		usage ();
		return 0;
	}
	usage ();
	return 1;
}

int hostmon_main (int argc, char **argv)
{
	int i, j, outc;
	char *clean_argv[256];
	for (i = 1; i < argc; i++)
	{
		if (!strcmp (argv[i], "--config") && i + 1 < argc)
		{
			g_config_path = argv[i + 1];
			break;
		}
	}
	if (load_profiles_config (g_config_path) == 0)
		g_config_loaded = 1;
	if (argc < 2)
	{
		usage ();
		return 1;
	}
	outc = 0;
	for (j = 0; j < argc && outc < 255; j++)
	{
		if (!strcmp (argv[j], "--config") && j + 1 < argc)
		{
			j++;
			continue;
		}
		clean_argv[outc++] = argv[j];
	}
	clean_argv[outc] = NULL;
	(void)g_config_loaded;
	return run_cmd (outc, clean_argv);
}
