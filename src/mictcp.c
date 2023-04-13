#include <mictcp.h>
#include <api/mictcp_core.h>

mic_tcp_sock sockets[MICTCP_SOCKETS];
mic_tcp_sock_addr connections[MICTCP_SOCKETS];
unsigned int seq_send[MICTCP_SOCKETS];
unsigned int seq_recv[MICTCP_SOCKETS];
unsigned int window[MICTCP_SOCKETS];
unsigned int window_loss[MICTCP_SOCKETS];
int socketd = 0;
int current_socket = MICTCP_SOCKETS;

const unsigned int MAX_WINDOW_LOSS = (unsigned int)((double)MICTCP_FENETRE * (1.0 - (MICTCP_FIABILITE / 100.1)));

/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
	MICTCP_DEBUG_APPEL;
	set_loss_rate(2);
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
	seq_send[d] = MICTCP_SEQUENCE_INITIALE;
	seq_recv[d] = MICTCP_SEQUENCE_INITIALE;
	// Initialisation des pertes.
	window[d] = 0;
	window_loss[d] = 0;
	return d;
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
{
	MICTCP_DEBUG_APPEL;
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
	MICTCP_DEBUG_APPEL;
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
	MICTCP_DEBUG_APPEL;
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
	MICTCP_DEBUG_APPEL;
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
		// Mise à jour de la fenêtre associée au socket.
		if (++window[socket] >= MICTCP_FENETRE)
		{
			window[socket] = 0;
			window_loss[socket] = 0;
		}
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
				// Si ACK reçu et que la séquence correspond ou que la perte est admissible, arrêt.
				if (result == 0 && pdu_ack.header.ack == 1 && pdu_ack.header.ack_num == seq_send[socket])
					resend = 0;
				// Sinon, on enregistre une nouvelle perte.
				else if (perte == 0)
				{
					perte = 1;
					// Si la perte est admissible, on l'ignore.
					if (++window_loss[socket] <= MAX_WINDOW_LOSS)
						resend = 0;
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
	MICTCP_DEBUG_APPEL;
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
	MICTCP_DEBUG_APPEL;
	if (socket >= 0 && socket < socketd && sockets[socket].state == ESTABLISHED)
	{
		sockets[socket].state = CLOSING;
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
	MICTCP_DEBUG_APPEL;
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
		// Mise à jour de la fenêtre associée au socket.
		if (++window[current_socket] >= MICTCP_FENETRE)
		{
			window[current_socket] = 0;
			window_loss[current_socket] = 0;
		}
		int maj_seq = 1;
		// Si la séquence est celle attendue, envoi dans le buffer.
		if (pdu.header.seq_num == seq_recv[current_socket])
			app_buffer_put(pdu.payload);
		// Sinon, on enregistre une perte.
		// Si elle est admissible, on l'ignore.
		else if (++window_loss[current_socket] <= MAX_WINDOW_LOSS)
			maj_seq = 0;
		// Passage à la séquence suivante.
		if (maj_seq)
		{
			seq_recv[current_socket] = (seq_recv[current_socket] + 1) % 2;
			pdu_ack.header.ack_num = seq_recv[current_socket];
		}
		// Envoi du ACK.
		IP_send(pdu_ack, addr);
	}
}
