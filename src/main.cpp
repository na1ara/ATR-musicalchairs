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
std::mutex cout_mutex; 
std::atomic<bool> musica_parada{false};
std::atomic<bool> jogo_ativo{true};
std::atomic<int> numero_cadeira = 1; 

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
class JogoDasCadeiras
{
public:
    JogoDasCadeiras(int num_jogadores)
        : cadeiras(num_jogadores - 1) {}

    void iniciar_rodada(int jogadores_ativos){
        // TODO: Inicia uma nova rodada, removendo uma cadeira e ressincronizando o semáforo
        cadeiras--;
        numero_cadeira = 1;  // Reinicia a contagem de cadeiras ocupadas
        
        for (bool drenando = true; drenando; ) {
            if (!cadeira_sem.try_acquire()) drenando = false;
        }
        
        cadeira_sem.release(cadeiras);
        musica_parada.store(false); // Nova rodada

        if (jogadores_ativos > 1){
            std::cout << "\nPróxima rodada com " << jogadores_ativos << " jogadores e " << cadeiras << " cadeiras.\n" << "A música está tocando... 🎵\n\n";
        }
    }

    void parar_musica(){
        // TODO: Simula o momento em que a música para e notifica os jogadores via variável de condição

        { // bloqueio atomico
            std::unique_lock<std::mutex> lock(music_mutex);
            musica_parada.store(true, std::memory_order_release);
        }
        
        music_cv.notify_all(); //avisa que a musica parou
        std::cout << "> A música parou! Os jogadores estão tentando se sentar...\n\n" << "----------------------------------------------------------\n";
    }

    void eliminar_jogador(int jogador_id, int num_jogadores) {
        // TODO: Elimina um jogador que não conseguiu uma cadeira
        std::lock_guard<std::mutex> lock(controle_mutex);
        if(!eliminados[jogador_id - 1]) {
            eliminados[jogador_id - 1] = true;
            num_jogadores--;
        }
    }

    void exibir_estado(){
        // TODO: Exibe o estado atual das cadeiras e dos jogadores
        std::cout << "Rodada atual com " << cadeiras << " cadeiras disponíveis.\n";
    }

    bool jogo_ativo(int jogadores_ativos) const{
        return jogadores_ativos > 1; //se tem mais de um entao ninguem ganhou ainda
    }

private:
    int num_jogadores;
    int cadeiras;
    std::vector<bool> eliminados;
    std::mutex controle_mutex;
};

class Jogador
{
public:
    Jogador(int id)
        : id(id), ativo(true), tentou_rodada(false) {}
        
    // Construtor de cópia
    Jogador(const Jogador& other)
        : id(other.id), ativo(other.ativo.load()), tentou_rodada(other.tentou_rodada.load()) {}

    // Construtor de movimentação
    Jogador(Jogador&& other) noexcept
        : id(other.id), ativo(other.ativo.load()), tentou_rodada(other.tentou_rodada.load()) {}

    bool esta_ativo() const{
        return ativo;
    }

    int get_id() const{
        return id;
    }

    void reseta_rodada(){
        tentou_rodada = false;
    }

    void tentar_ocupar_cadeira(){
        // TODO: Tenta ocupar uma cadeira utilizando o semáforo contador quando a música para (aguarda pela variável de condição)
        // if (ativo && !tentou_rodada){
        //     tentou_rodada = true; 
        //     verificar_eliminacao();
        // }
        if (ativo.load(std::memory_order_acquire) && !tentou_rodada.exchange(true, std::memory_order_acq_rel)) {
            verificar_eliminacao();
        }
    }

    void verificar_eliminacao(){
        // TODO: Verifica se foi eliminado após ser destravado do semáforo
        // if (cadeira_sem.try_acquire()){
        //     std::cout << "[Cadeira " << numero_cadeira++ << "]: Ocupada por P" << id << "\n";
        // } else { 
        //     ativo = false; 
        //     std::cout << "\nJogador P" << id << " não conseguiu uma cadeira e foi eliminado!\n";
        //     std::cout << "----------------------------------------------------------\n";
        // }
        if (cadeira_sem.try_acquire()) {
            std::lock_guard<std::mutex> cout_lock(cout_mutex);
            std::cout << "[Cadeira " << numero_cadeira.fetch_add(1, std::memory_order_relaxed) + 1 << "]: Ocupada por P" << id << "\n";
        } else {
            ativo.store(false, std::memory_order_release);
            std::lock_guard<std::mutex> cout_lock(cout_mutex);
            std::cout << "\nJogador P" << id << " não conseguiu uma cadeira e foi eliminado!\n" << "----------------------------------------------------------\n";
        }
    }

    void joga(){
        //  while (ativo && jogo_ativo.load()){  // Verifica se o jogo ainda está ativo
        //     std::unique_lock<std::mutex> lock(music_mutex);
        //     music_cv.wait(lock, [] { return musica_parada.load() || !jogo_ativo.load(); });

        //     if (!jogo_ativo.load()) break;  // Termina a execução se o jogo acabou

        //     tentar_ocupar_cadeira();
        // }
        while (ativo.load(std::memory_order_acquire) && 
              jogo_ativo.load(std::memory_order_acquire)) {
            
            std::unique_lock<std::mutex> lock(music_mutex);
            music_cv.wait(lock, [this] { 
                return musica_parada.load(std::memory_order_acquire) || 
                      !jogo_ativo.load(std::memory_order_acquire); 
            });

            if (!jogo_ativo.load(std::memory_order_acquire)) break;

            tentar_ocupar_cadeira();
        }
    }

private:
    int id;
    std::atomic<bool> ativo;
    std::atomic<bool> tentou_rodada;
};

class Coordenador{
public:
    Coordenador(JogoDasCadeiras &jogo, std::vector<Jogador> &jogadores)
        : jogo(jogo), jogadores(jogadores) {}

    void iniciar_jogo(){
        // TODO: Começa o jogo, dorme por um período aleatório, e então para a música, sinalizando os jogadores 
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(1000, 3000);

        while (jogo.jogo_ativo(jogadores_ativos())){
            std::this_thread::sleep_for(std::chrono::milliseconds(dist(gen)));
            jogo.parar_musica();

            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            liberar_threads_eliminadas();
            jogo.iniciar_rodada(jogadores_ativos());
            reseta_rodada_jogadores();
        }

        std::cout << "\n🏆 Vencedor: Jogador P" << encontrar_vencedor() << "! Parabéns! 🏆\n\n";
        std::cout << "----------------------------------------------------------\n";

        jogo_ativo.store(false);
        music_cv.notify_all();
    }

    void liberar_threads_eliminadas(){
        // Libera múltiplas permissões no semáforo para destravar todas as threads que não conseguiram se sentar
        cadeira_sem.release(NUM_JOGADORES - 1); // Libera o número de permissões igual ao número de jogadores que ficaram esperando
    }

    int jogadores_ativos() const{
        int ativos = 0;
        for (const auto &jogador : jogadores){
            if (jogador.esta_ativo()){
                ativos++;
            }
        }
        return ativos;
    }

    int encontrar_vencedor() const{
        for (const auto &jogador : jogadores){
            if (jogador.esta_ativo()){
                return jogador.get_id();
            }
        }
        return -1;
    }

    void reseta_rodada_jogadores(){
        for (auto &jogador : jogadores){
            jogador.reseta_rodada();
        }
    }

private:
    JogoDasCadeiras &jogo;
    std::vector<Jogador> &jogadores;
};

// Main function
int main(){
    std::cout << "----------------------------------------------------------\n";
    std::cout << "Bem-vindo ao Jogo das Cadeiras Concorrente!\n";
    std::cout << "----------------------------------------------------------\n";

    std::cout << "\nIniciando rodada com " << NUM_JOGADORES << " jogadores e " << NUM_JOGADORES - 1 << " cadeiras\n";
    std::cout << "A música está tocando... 🎵\n\n";

    JogoDasCadeiras jogo(NUM_JOGADORES);
    std::vector<Jogador> jogadores;

    // Criação das threads dos jogadores
    for (int i = 1; i <= NUM_JOGADORES; ++i){
        jogadores.emplace_back(i);
    }

    Coordenador coordenador(jogo, jogadores);
    std::vector<std::thread> threads_jogadores;

    for (auto &jogador : jogadores){
        threads_jogadores.emplace_back(&Jogador::joga, &jogador);
    }

    // Thread do coordenador
    std::thread thread_coordenador(&Coordenador::iniciar_jogo, &coordenador);

    // Esperar pelas threads dos jogadores
    for (auto &t : threads_jogadores) {
        if (t.joinable()){
            t.join();
        }
    }

    // Esperar pela thread do coordenador
    if (thread_coordenador.joinable()){
        thread_coordenador.join();
    }

    std::cout << "\nObrigado por jogar o Jogo das Cadeiras Concorrente!\n\n";

    return 0;
}
