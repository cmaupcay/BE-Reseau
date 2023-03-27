#include <mictcp.h>
#include <api/mictcp_core.h>

unsigned int next_seq = 0;
const unsigned long timeout = 100;

/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
	int result = -1;
	printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
	result = initialize_components(sm); /* Appel obligatoire */
	set_loss_rate(0);

	return result;
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
{
	printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
	return 0;
}

/*
 * Met le socket en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr)
{
	printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
	return 0;
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{
	printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
	return 0;
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size)
{
	printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
	mic_tcp_sock_addr addr = {0};
	mic_tcp_pdu pdu = {0}, pdu_ack = {0};
	pdu.payload.data = mesg;
	pdu.payload.size = mesg_size;
	pdu.header.seq_num = next_seq;
	int result = -1, resend = 1;
	do
	{
		result = IP_send(pdu, addr);
		resend = IP_recv(&pdu_ack, &addr, timeout);
		if (resend != -1 && pdu_ack.header.ack == 1 && pdu_ack.header.ack_num == pdu.header.seq_num)
		{
			next_seq = (next_seq + 1) % 2;
			resend = 0;
		}
	}
	while (resend != 0);
	return result;
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
	mic_tcp_payload payload = {0};
	payload.data = mesg;
	payload.size = max_mesg_size;
	return app_buffer_get(payload);
}

/*
 * Permet de réclamer la destruction d’un socket.
 * Engendre la fermeture de la connexion suivant le modèle de TCP.
 * Retourne 0 si tout se passe bien et -1 en cas d'erreur
 */
int mic_tcp_close (int socket)
{
	printf("[MIC-TCP] Appel de la fonction :  "); printf(__FUNCTION__); printf("\n");
	return 0;
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
	mic_tcp_pdu pdu_ack = {0};
	pdu_ack.header.ack = 1;
	if (pdu.header.seq_num == next_seq)
	{
		app_buffer_put(pdu.payload);
		pdu_ack.header.ack_num = next_seq;
		next_seq = (next_seq + 1) % 2;
	}
	IP_send(pdu_ack, addr);
}
