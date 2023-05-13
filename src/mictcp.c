#include <mictcp.h>
#include <api/mictcp_core.h>
#include <limits.h>

mic_tcp_sock sockets[MICTCP_SOCKETS];
mic_tcp_sock_addr connections[MICTCP_SOCKETS];
unsigned int seq_send[MICTCP_SOCKETS];
unsigned int seq_recv[MICTCP_SOCKETS];
unsigned int loss_distance[MICTCP_SOCKETS];
int socketd = 0;
int current_socket = MICTCP_SOCKETS;

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
	seq_send[d] = MICTCP_INITIAL_SEQ;
	seq_recv[d] = MICTCP_INITIAL_SEQ;
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
	if (socket >= 0 && socket < socketd)
	{
		connections[socket] = *addr;
		sockets[socket].state = ESTABLISHED;
		return 0;
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
	if (socket >= 0 && socket < socketd)
	{
		connections[socket] = addr;
		sockets[socket].state = ESTABLISHED;
		return 0;
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
				.seq_num = seq_send[socket],
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
		// Mise à jour du numéro de séquence à émettre.
		seq_send[socket] = (seq_send[socket] + 1) % 2;
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
				result = IP_recv(&pdu_ack, &connections[socket], MICTCP_TIMEOUT);
				// Si ACK reçu et que la séquence correspond, arrêt.
				if (result == 0)
				{
					if (pdu_ack.header.ack == 1 && pdu_ack.header.ack_num == seq_send[socket])
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
						printf("Lost packet ");
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
				printf(	"%d sent, %d lost (%f%c), %d resent (%f%c).",
						sent, lost, ((double)lost / (double) sent) * 100.0, '%',
						resent, ((double)resent / (double)lost) * 100.0, '%');
		#endif
		sockets[socket].state = CLOSED;
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
	if (current_socket < MICTCP_SOCKETS)
	{
		mic_tcp_pdu pdu_ack = {
			.header = {
				.source_port = pdu.header.dest_port,
				.dest_port = pdu.header.source_port,
				.seq_num = __UINT32_MAX__,
				.ack_num = seq_recv[current_socket],
				.syn = 0,
				.ack = 1,
				.fin = 0
			}
		};
		// Si la séquence est celle attendue, traitement de la trame.
		if (pdu.header.seq_num == seq_recv[current_socket])
		{
			app_buffer_put(pdu.payload);
			// Passage à la séquence suivante.
			seq_recv[current_socket] = (seq_recv[current_socket] + 1) % 2;
			pdu_ack.header.ack_num = seq_recv[current_socket];
		}
		#ifdef MICTCP_DEBUG_REJECTED
			else printf("Packet #%d rejected.\n", pdu.header.seq_num);
		#endif
		// Envoi du ACK.
		IP_send(pdu_ack, addr);
	}
}
