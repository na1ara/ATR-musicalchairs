#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <semaphore>
#include <atomic>
#include <chrono>
#include <random>

// Global variables for synchronization
constexpr int NUM_JOGADORES = 4;
std::counting_semaphore<NUM_JOGADORES> cadeira_sem(NUM_JOGADORES - 1); // Inicia com n-1 cadeiras, capacidade máxima n
std::condition_variable music_cv;
std::mutex music_mutex;
std::atomic<bool> musica_parada{false};
std::atomic<bool> jogo_ativo{true};

/*
 * Uso básico de um counting_semaphore em C++:
 * 
 * O `std::counting_semaphore` é um mecanismo de sincronização que permite controlar o acesso a um recurso compartilhado 
 * com um número máximo de acessos simultâneos. Neste projeto, ele é usado para gerenciar o número de cadeiras disponíveis.
 * Inicializamos o semáforo com `n - 1` para representar as cadeiras disponíveis no início do jogo. 
 * Cada jogador que tenta se sentar precisa fazer um `acquire()`, e o semáforo permite que até `n - 1` jogadores 
 * ocupem as cadeiras. Quando todos os assentos estão ocupados, jogadores adicionais ficam bloqueados até que 
 * o coordenador libere o semáforo com `release()`, sinalizando a eliminação dos jogadores.
 * O método `release()` também pode ser usado para liberar múltiplas permissões de uma só vez, por exemplo: `cadeira_sem.release(3);`,
 * o que permite destravar várias threads de uma só vez, como é feito na função `liberar_threads_eliminadas()`.
 *
 * Métodos da classe `std::counting_semaphore`:
 * 
 * 1. `acquire()`: Decrementa o contador do semáforo. Bloqueia a thread se o valor for zero.
 *    - Exemplo de uso: `cadeira_sem.acquire();` // Jogador tenta ocupar uma cadeira.
 * 
 * 2. `release(int n = 1)`: Incrementa o contador do semáforo em `n`. Pode liberar múltiplas permissões.
 *    - Exemplo de uso: `cadeira_sem.release(2);` // Libera 2 permissões simultaneamente.
 */

// Classes
class JogoDasCadeiras {
public:
    JogoDasCadeiras(int num_jogadores)
        : num_jogadores(num_jogadores), 
        cadeiras(num_jogadores - 1),
        jogadores_restantes(num_jogadores),
        eliminados(num_jogadores, false) {}

    void iniciar_rodada() {
        // TODO: Inicia uma nova rodada, removendo uma cadeira e ressincronizando o semáforo
        cadeiras = jogadores_restantes - 1;
        cadeira_sem.release(cadeiras);
    }

    void parar_musica() {
        // TODO: Simula o momento em que a música para e notifica os jogadores via variável de condição
        musica_parada.store(true);
        music_cv.notify_all();
    }

    void eliminar_jogador(int jogador_id) {
        // TODO: Elimina um jogador que não conseguiu uma cadeira
        std::lock_guard<std::mutex> lock(music_mutex);
        if (!eliminados[jogador_id - 1]) {
            eliminados[jogador_id - 1] = true;
            jogadores_restantes--;
            std::cout << "Jogador P" << jogador_id << " não conseguiu uma cadeira e foi eliminado!\n";
        }
    }

    void exibir_estado() {
        // TODO: Exibe o estado atual das cadeiras e dos jogadores
        std::lock_guard<std::mutex> lock(music_mutex);
        std::cout << "\n-----------------------------------------------\n";
        std::cout << "Jogadores restantes: " << jogadores_restantes 
             << " | Cadeiras disponíveis: " << cadeiras << std::endl;
    }

    int get_jogadores_restantes() const { return jogadores_restantes; }
    int get_cadeiras() const { return cadeiras; }

private:
    int num_jogadores;
    int cadeiras;
    int jogadores_restantes;
    std::vector<bool> eliminados;
};

class Jogador {
public:
    Jogador(int id, JogoDasCadeiras& jogo)
        : id(id), jogo(jogo) {}

    void tentar_ocupar_cadeira() {
        // TODO: Tenta ocupar uma cadeira utilizando o semáforo contador quando a música para (aguarda pela variável de condição)
        cadeira_sem.acquire(); // Bloqueia até conseguir uma cadeira
        
        std::lock_guard<std::mutex> lock(music_mutex);
        if (musica_parada) {
            std::cout << "[Cadeira]: Ocupada por P" << id << std::endl;
        }
    }

    void verificar_eliminacao() {
        // TODO: Verifica se foi eliminado após ser destravado do semáforo
        if (!musica_parada) {
            jogo.eliminar_jogador(id);
        }
    }

    void joga() {
        // TODO: Aguarda a música parar usando a variavel de condicao
        while(jogo_ativo){
            std::unique_lock<std::mutex> lock(music_mutex);
            music_cv.wait(lock, [] { return musica_parada.load() || !jogo_ativo.load(); });
            if (!jogo_ativo) break;
        
        // TODO: Tenta ocupar uma cadeira
        tentar_ocupar_cadeira();
        
        // TODO: Verifica se foi eliminado
        verificar_eliminacao();
        }

    }

private:
    int id;
    JogoDasCadeiras& jogo;
};

class Coordenador {
public:
    Coordenador(JogoDasCadeiras& jogo)
        : jogo(jogo) {}

    void iniciar_jogo() {
        // TODO: Começa o jogo, dorme por um período aleatório, e então para a música, sinalizando os jogadores 
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> sleep_time(1000, 3000);

        while (jogo.get_jogadores_restantes() > 1) {
            // Fase: música tocando
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time(gen)));
            
            // Fase: música para
            jogo.parar_musica();
            
            // Tempo para os jogadores tentarem se sentar
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Liberar jogadores eliminados
            int eliminacoes = jogo.get_jogadores_restantes() - jogo.get_cadeiras();
            cadeira_sem.release(eliminacoes);
            
            // Resetar para próxima rodada
            musica_parada.store(false);
            jogo.iniciar_rodada();
            jogo.exibir_estado();
        }
        
        jogo_ativo.store(false);
        music_cv.notify_all();
    }

    void liberar_threads_eliminadas() {
        // Libera múltiplas permissões no semáforo para destravar todas as threads que não conseguiram se sentar
        // Implementado em iniciar_jogo()
    }

private:
    JogoDasCadeiras& jogo;
};

// Main function
int main() {
    JogoDasCadeiras jogo(NUM_JOGADORES);
    Coordenador coordenador(jogo);
    std::vector<std::thread> jogadores;

    // Criação das threads dos jogadores
    std::vector<Jogador> jogadores_objs;
    for (int i = 1; i <= NUM_JOGADORES; ++i) {
        jogadores_objs.emplace_back(i, jogo);
    }

    for (int i = 0; i < NUM_JOGADORES; ++i) {
        jogadores.emplace_back(&Jogador::joga, &jogadores_objs[i]);
    }

    std::vector<std::thread> threads_jogadores;
    for (auto& jogador : jogadores_objs) {
        threads_jogadores.emplace_back(&Jogador::joga, &jogador);
    }

    // Thread do coordenador
    std::thread coordenador_thread(&Coordenador::iniciar_jogo, &coordenador);

    // Esperar pelas threads dos jogadores
    for (auto& t : jogadores) {
        if (t.joinable()) {
            t.join();
        }
    }

    // Esperar pela thread do coordenador
    if (coordenador_thread.joinable()) {
        coordenador_thread.join();
    }

    std::cout << "\n🏆 Vencedor: ";
    for (int i = 0; i < NUM_JOGADORES; ++i) {
        if (!jogo.get_jogadores_restantes() || (i == NUM_JOGADORES - 1)) {
            std::cout << "P" << (i + 1) << "! Parabéns! 🏆\n";
        }
    }

    std::cout << "Jogo das Cadeiras finalizado." << std::endl;
    return 0;
}

