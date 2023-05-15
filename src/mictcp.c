#include <mictcp.h>
#include <api/mictcp_core.h>
#include <limits.h>

// Sockets.
mic_tcp_sock sockets[MICTCP_SOCKETS];
// Adresses distantes.
mic_tcp_sock_addr connections[MICTCP_SOCKETS];
// Numéros de séquence.
unsigned int seq[MICTCP_SOCKETS];
// Distances de perte.
unsigned int loss_distance[MICTCP_SOCKETS];
// Descripteur du prochain socket.
int socketd = 0;
// Socket sélectionné.
int current_socket = MICTCP_SOCKETS;

// Distance maximale de perte.
const unsigned int LOSS_DISTANCE_MAX = MICTCP_RELIABILITY > 0 ? (unsigned int)((double)MICTCP_WINDOW * (1.0 - (double)MICTCP_RELIABILITY / 100.01)) : UINT_MAX;

#ifdef MICTCP_DEBUG_RELIABILITY
	unsigned int sent = 0, lost = 0, resent = 0;
#endif

/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
	MICTCP_DEBUG_FUNCTION;
	set_loss_rate(MICTCP_LOSS_RATE);
	if (initialize_components(sm) == -1) return -1;
	// Recherche d'un descripteur libre.
	int d;
	for (d = 0; d < socketd; d++)
		if (sockets[d].state == CLOSED)
			break;
	// Si aucun descripteur disponible, on en créer un.
	if (d == socketd)
	{
		socketd++;
		if (socketd >= MICTCP_SOCKETS)
			return -1;
	}
	// Initialisation du socket.
	sockets[d].fd = d;
	sockets[d].state = IDLE;
	// Initialisation des numéros de séquence.
	seq[d] = MICTCP_INITIAL_SEQ;
	// Initialisation des pertes.
	loss_distance[d] = 0;
	return d;
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
{
	MICTCP_DEBUG_FUNCTION;
	if (socket >= 0 && socket < socketd)
	{
		sockets[socket].addr = addr;
		return 0;
	}
	return -1;
}

/*
 * Met le socket en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr)
{
	MICTCP_DEBUG_FUNCTION;
	if (socket >= 0 && socket < socketd && sockets[socket].state == IDLE)
	{
		// Attente d'un SYN.
		mic_tcp_pdu pdu_remote = {0};
		int result = -1;
		do result = IP_recv(&pdu_remote, addr, MICTCP_TIMEOUT_CONNECT * MICTCP_RETRIES);
		while (result < 0);
		if (result >= 0 && pdu_remote.header.syn == 1)
		{
			sockets[socket].state = SYN_RECEIVED;
			// Définition du numéro de séquence.
			seq[socket] = (pdu_remote.header.seq_num + 1) % 2;
			// Envoi du SYN ACK.
			mic_tcp_pdu pdu = {
				.header = {
					.source_port = sockets[socket].addr.port,
					.dest_port = addr->port,
					.seq_num = pdu_remote.header.seq_num,
					.ack_num = seq[socket],
					.syn = 1,
					.ack = 1,
					.fin = 0
				},
				.payload.size = 0
			};
			do
			{
				result = IP_send(pdu, *addr);
				if (result >= 0)
				{
					// Attente du ACK.
					result = IP_recv(&pdu_remote, addr, MICTCP_TIMEOUT_ACK);
					if (result <= 0)
					{
						if (pdu.header.ack == 1 && pdu.header.ack_num == seq[socket])
						{
							// Connexion établie.
							connections[socket] = *addr;
							sockets[socket].state = ESTABLISHED;
							#ifdef MICTCP_DEBUG_CONNECTION
								printf("Connection established.\n");
							#endif
							return 0;
						}
						#ifdef MICTCP_DEBUG_REJECTED
							else printf("Packet #%d rejected.\n", pdu.header.ack_num);
						#endif
					}
				}
			}
			while (result < 0);
		}
	}
	return -1;
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{
	MICTCP_DEBUG_FUNCTION;
	if (socket >= 0 && socket < socketd && sockets[socket].state == IDLE)
	{
		mic_tcp_pdu pdu = {
			.header = {
				.source_port = sockets[socket].addr.port,
				.dest_port = addr.port,
				.seq_num = seq[socket],
				.ack_num = __UINT32_MAX__,
				.syn = 1,
				.ack = 0,
				.fin = 0
			},
			.payload.size = 0
		}, pdu_ack = {0};
		// Mise à jour du numéro de séquence.
		seq[socket] = (seq[socket] + 1) % 2;
		// Envoi du SYN.
		sockets[socket].state = SYN_SENT;
		int tries = 0, result = -1;
		do
		{
			result = IP_send(pdu, addr);
			// Attente du SYN ACK.
			if (result >= 0) result = IP_recv(&pdu_ack, &addr, MICTCP_TIMEOUT_CONNECT);
		}
		while (result < 0 && ++tries < MICTCP_RETRIES);
		if (result >= 0)
		{
			if (pdu_ack.header.syn == 1 && pdu_ack.header.ack == 1 && pdu_ack.header.ack_num == seq[socket])
			{
				// Envoi du ACK.
				pdu.header.seq_num = seq[socket];
				pdu.header.ack_num = seq[socket];
				pdu.header.syn = 0;
				pdu.header.ack = 1;
				do result = IP_send(pdu, addr);
				while (result < 0);
				// Connexion établie.
				connections[socket] = addr;
				sockets[socket].state = ESTABLISHED;
				#ifdef MICTCP_DEBUG_CONNECTION
					printf("Connection established.\n");
				#endif
				return 0;
			}
			else printf("Connection refused.\n");
		}
	}
	return -1;
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int socket, char* mesg, int mesg_size)
{
	MICTCP_DEBUG_FUNCTION;
	if (socket >= 0 && socket < socketd && sockets[socket].state == ESTABLISHED)
	{
		mic_tcp_pdu pdu = {
			.header = {
				.source_port = sockets[socket].addr.port,
				.dest_port = connections[socket].port,
				.seq_num = seq[socket],
				.ack_num = __UINT32_MAX__,
				.syn = 0,
				.ack = 0,
				.fin = 0
			},
			.payload = {
				.data = mesg,
				.size = mesg_size
			}
		}, pdu_ack = {0};
		// Mise à jour du numéro de séquence.
		seq[socket] = (seq[socket] + 1) % 2;
		// Mise à jour des pertes.
		loss_distance[socket]++;
		#ifdef MICTCP_DEBUG_RELIABILITY
			sent++;
		#endif
		int result = -1, resend = 1, perte = 0;
		do
		{
			pdu_ack.header.ack = 0;
			pdu_ack.header.ack_num = __UINT32_MAX__;
			// Envoi du PDU.
			result = IP_send(pdu, connections[socket]);
			if (result == mesg_size)
			{
				// Attente du ACK.
				result = IP_recv(&pdu_ack, &connections[socket], MICTCP_TIMEOUT_ACK);
				// Si ACK reçu et que la séquence correspond, arrêt.
				if (result == 0)
				{
					if (pdu_ack.header.ack == 1 && pdu_ack.header.ack_num == seq[socket])
						resend = 0;
					#ifdef MICTCP_DEBUG_REJECTED
						else printf("ACK#%d packet rejected.\n", pdu_ack.header.ack_num);
					#endif
				}
				// Sinon, on enregistre une perte.
				else
				{
					// Si c'est la première perte pour ce paquet, on défini si on doit le renvoyer.
					if (perte == 0)
					{
						perte = 1;
						// Si la perte n'est pas admissible, on réinitialise la distance de perte.
						#if MICTCP_RELIABILITY > 0
							if (loss_distance[socket] > LOSS_DISTANCE_MAX)
								loss_distance[socket] = 0;
							// Sinon, on l'ignore.
							else resend = 0;
						#else
							resend = 0;
						#endif
					}
					#ifdef MICTCP_DEBUG_LOSS
						printf("Lost packet #%d ", pdu.header.seq_num);
						printf(resend == 0 ? "ignored" : "resent");
						printf(".\n");
					#endif
					#ifdef MICTCP_DEBUG_RELIABILITY
						lost++;
						if (resend) resent++;
					#endif
				}
			}
		}
		while (resend == 1);
		return mesg_size;
	}
	return -1;
}

/*
 * Permet à l’application réceptrice de réclamer la récupération d’une donnée
 * stockée dans les buffers de réception du socket
 * Retourne le nombre d’octets lu ou bien -1 en cas d’erreur
 * NB : cette fonction fait appel à la fonction app_buffer_get()
 */
int mic_tcp_recv (int socket, char* mesg, int max_mesg_size)
{
	MICTCP_DEBUG_FUNCTION;
	if (socket >= 0 && socket < socketd && sockets[socket].state == ESTABLISHED)
	{
		mic_tcp_payload payload = {
			.data = mesg,
			.size = max_mesg_size
		};
		// Définition du descripteur du socket de réception.
		current_socket = socket;
		return app_buffer_get(payload);
	}
	return -1;
}

/*
 * Permet de réclamer la destruction d’un socket.
 * Engendre la fermeture de la connexion suivant le modèle de TCP.
 * Retourne 0 si tout se passe bien et -1 en cas d'erreur
 */
int mic_tcp_close (int socket)
{
	MICTCP_DEBUG_FUNCTION;
	if (socket >= 0 && socket < socketd && sockets[socket].state == ESTABLISHED)
	{
		sockets[socket].state = CLOSING;
		#ifdef MICTCP_DEBUG_RELIABILITY
			if (sent > 0)
				printf(	"%d sent, %d lost (lost / send = %f%c), %d resent (resent / lost = %f%c) -> 1 - (lost - resent) / sent = %f%c\n",
						sent, lost, ((double)lost / (double)sent) * 100.0, '%',
						resent, ((double)resent / (double)lost) * 100.0, '%',
						(1.0 - ((double)(lost - resent) / (double)sent)) * 100.0, '%'
					);
		#endif
		sockets[socket].state = CLOSED;
		#ifdef MICTCP_DEBUG_CONNECTION
			printf("Connection closed.\n");
		#endif
		return 0;
	}
	return -1;
}

/*
 * Traitement d’un PDU MIC-TCP reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) puis insère les données utiles du PDU dans
 * le buffer de réception du socket. Cette fonction utilise la fonction
 * app_buffer_put().
 */
void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_sock_addr addr)
{
	MICTCP_DEBUG_FUNCTION;
	if (current_socket < MICTCP_SOCKETS && sockets[current_socket].state == ESTABLISHED)
	{
		mic_tcp_pdu pdu_ack = {
			.header = {
				.source_port = pdu.header.dest_port,
				.dest_port = pdu.header.source_port,
				.seq_num = __UINT32_MAX__,
				.ack_num = seq[current_socket],
				.syn = 0,
				.ack = 1,
				.fin = 0
			}
		};
		// Si la séquence est celle attendue, traitement de la trame.
		if (pdu.header.seq_num == seq[current_socket])
		{
			app_buffer_put(pdu.payload);
			// Passage à la séquence suivante.
			seq[current_socket] = (seq[current_socket] + 1) % 2;
			pdu_ack.header.ack_num = seq[current_socket];
		}
		#ifdef MICTCP_DEBUG_REJECTED
			else printf("Packet #%d rejected.\n", pdu.header.seq_num);
		#endif
		// Envoi du ACK.
		IP_send(pdu_ack, addr);
	}
	#ifdef MICTCP_DEBUG_REJECTED
		else printf("Packet #%d ignored.\n", pdu.header.seq_num);
	#endif
}
