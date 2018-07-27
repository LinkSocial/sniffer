#define NUM_STATES 5
#define NUM_EVENTS 5

// definicao dos possiveis eventos gerados pelos estados da FSM
typedef enum event_ {empty, wait, success, error, connected} event;
// definicao das funcoes que implementam o comportamento de cada estado
event config_board_state(void);
event check_probe_num_state(void);
event connect_state(void);
event send_data_state(void);
event end_state(void);
// array de ponteiros para as funcoes dos estados
event (* state_functions[])(void) = {config_board_state, check_probe_num_state, connect_state, send_data_state, end_state};
// definicao dos nomes dos estados
typedef enum state_ {config_board, check_probe_num, connect, send_data, end} state;
// estrutura que define as transicoes dos estados
state state_transitions[NUM_STATES][NUM_EVENTS] = {{check_probe_num, end, end, end, end},
                                                   {end, check_probe_num, connect, end, end},
                                                   {end, end, end, connect, send_data},
                                                   {end, end, config_board, connect, end},
                                                   {end, end, end, end, end}};
// definicao dos estados inicial e final
#define EXIT_STATE end
#define ENTRY_STATE config_board

// funcao que implementa a ransicao de estados
state lookup_transitions(state cur_state, event cur_evt) {
  return state_transitions[cur_state][cur_evt];
}
