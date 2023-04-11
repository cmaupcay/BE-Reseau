#include <mictcp.h>
#include <api/mictcp_core.h>

mic_tcp_sock sockets[MICTCP_SOCKETS];
mic_tcp_sock_addr* connections[MICTCP_SOCKETS];
unsigned int seq_emission[MICTCP_SOCKETS];
unsigned int seq_reception[MICTCP_SOCKETS];
unsigned int* current_seq_reception = NULL;
int socketd = 0;

/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
	printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
	set_loss_rate(1);
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
	if (connections[d])
	{
		free(connections[d]);
		connections[d] = NULL;
	}
	// Initialisation des numéros de séquence.
	seq_emission[d] = MICTCP_SEQ;
	seq_reception[d] = MICTCP_SEQ;
	return d;
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
{
	printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
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
	printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
	if (socket >= 0 && socket < socketd)
	{
		connections[socket] = addr;
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
	printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
	if (socket >= 0 && socket < socketd)
	{
		if (connections[socket] == NULL)
			connections[socket] = (mic_tcp_sock_addr*)malloc(sizeof(mic_tcp_sock_addr));
		*connections[socket] = addr;
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
	printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
	if (socket >= 0 && socket < socketd && sockets[socket].state == ESTABLISHED)
	{
		mic_tcp_pdu pdu = {
			.header = {
				.source_port = sockets[socket].addr.port,
				.dest_port = connections[socket]->port,
				.seq_num = seq_emission[socket],
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
		seq_emission[socket] = (seq_emission[socket] + 1) % 2;
		int result = -1, resend = 1;
		do
		{
			pdu_ack.header.ack = 0;
			pdu_ack.header.ack_num = __UINT32_MAX__;
			// Envoi du PDU.
			result = IP_send(pdu, *connections[socket]);
			if (result == mesg_size)
			{
				// Attente du ACK.
				result = IP_recv(&pdu_ack, connections[socket], MICTCP_TIMEOUT);
				// Si ACK reçu et que la séquence correspond, arrêt.
				if (result == 0 && pdu_ack.header.ack == 1 && pdu_ack.header.ack_num == seq_emission[socket])
					resend = 0;
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
	printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
	if (socket >= 0 && socket < socketd && sockets[socket].state == ESTABLISHED)
	{
		mic_tcp_payload payload = {
			.data = mesg,
			.size = max_mesg_size
		};
		// Définition du numéro de séquence à utiliser.
		current_seq_reception = &seq_reception[socket];
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
	printf("[MIC-TCP] Appel de la fonction :  "); printf(__FUNCTION__); printf("\n");
	if (socket >= 0 && socket < socketd && sockets[socket].state == ESTABLISHED)
	{
		sockets[socket].state = CLOSING;
		if (connections[socket])
		{
			free(connections[socket]);
			connections[socket] = NULL;
		}
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
	printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
	if (current_seq_reception)
	{
		mic_tcp_pdu pdu_ack = {
			.header = {
				.source_port = pdu.header.dest_port,
				.dest_port = pdu.header.source_port,
				.seq_num = __UINT32_MAX__,
				.ack_num = *current_seq_reception,
				.syn = 0,
				.ack = 1,
				.fin = 0
			}
		};
		// Si la séquence est celle attendue, traitement.
		if (pdu.header.seq_num == *current_seq_reception)
		{
			app_buffer_put(pdu.payload);
			// Mise à jour du numéro de la séquence attendue.
			*current_seq_reception = (*current_seq_reception + 1) % 2;
			pdu_ack.header.ack_num = *current_seq_reception;
		}
		IP_send(pdu_ack, addr);
	}
}