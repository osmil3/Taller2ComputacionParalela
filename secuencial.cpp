#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <libssh/libssh.h>
#include <set>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <iomanip> //biblioteca para std::setprecision
// Estructura para almacenar la sesión SSH y el canal
struct SSHConnection {
    ssh_session session;
    ssh_channel channel;
};
struct Producto{
    std::string SKU;
    std::string nombre;
   std::string precio;
    int cantidad;
};
// Función para conectar al servidor SSH
int connect_ssh(ssh_session session, const std::string& host, const std::string& port, const std::string& username, const std::string& password) {
    // Establecer los parámetros de conexión SSH
    ssh_options_set(session, SSH_OPTIONS_HOST, host.c_str());
    ssh_options_set(session, SSH_OPTIONS_PORT_STR, port.c_str());
    ssh_options_set(session, SSH_OPTIONS_USER, username.c_str());

    // Conectar al servidor SSH
    int rc = ssh_connect(session);
    if (rc != SSH_OK) {
        std::cerr << "Error al conectar al servidor SSH: " << ssh_get_error(session) << std::endl;
        return 1;
    }
    // Autenticarse con el servidor SSH utilizando la contraseña
    rc = ssh_userauth_password(session, NULL, password.c_str());
    if (rc != SSH_AUTH_SUCCESS) {
        std::cerr << "Error al autenticarse con el servidor SSH: " << ssh_get_error(session) << std::endl;
        return 1;
    }
    return 0;
}

// Función para abrir un canal en la sesión SSH
int open_ssh_channel(ssh_session session, ssh_channel& channel) {
    channel = ssh_channel_new(session);
    int rc = ssh_channel_open_session(channel);
    if (rc != SSH_OK) {
        std::cerr << "Error al abrir el canal de sesión SSH: " << ssh_get_error(session) << std::endl;
        return 1;
    }

    return 0;
}

// Función para cerrar el canal SSH
void close_ssh_channel(ssh_channel& channel) {
    ssh_channel_send_eof(channel);
    ssh_channel_close(channel);
    ssh_channel_free(channel);
       
}


int ejecutarcomando(ssh_session session, const std::string& command, std::string& output) {
    ssh_channel channel = ssh_channel_new(session);
    int rc = ssh_channel_open_session(channel);
    if (rc != SSH_OK) {
        std::cerr << "Error al abrir el canal de sesión SSH: " << ssh_get_error(session) << std::endl;
        ssh_channel_free(channel);
        return 1;
    }

    rc = ssh_channel_request_exec(channel, command.c_str());
    if (rc != SSH_OK) {
        std::cerr << "Error al ejecutar el comando en el servidor SSH: " << ssh_get_error(session) << std::endl;
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        return 1;
    }

    char buffer[256];
    int nbytes;
    while ((nbytes = ssh_channel_read(channel, buffer, sizeof(buffer), 0)) > 0) {
        output.append(buffer, nbytes);
    }

    ssh_channel_send_eof(channel);
    ssh_channel_close(channel);
    ssh_channel_free(channel);

    return 0;
}


std::string filtrarPorEstados(const std::string& productosStr, const std::vector<std::string>& estadosPermitidos) {
    std::istringstream iss(productosStr);
    std::ostringstream filteredOutput;
    std::string line;
    while (std::getline(iss, line)) {
        std::istringstream lineIss(line);
        std::string sku, nombre, precio, cantidadStr, fecha, estado;
        if (std::getline(lineIss, sku, ';') &&
            std::getline(lineIss, nombre, ';') &&
            std::getline(lineIss, precio, ';') &&
            std::getline(lineIss, cantidadStr, ';') &&
            std::getline(lineIss, fecha, ';') &&
            std::getline(lineIss, estado, ';')) {
            estado = estado.substr(1, estado.size() - 2);  // Eliminar comillas al principio y al final de estado
            if (std::find(estadosPermitidos.begin(), estadosPermitidos.end(), estado) != estadosPermitidos.end()) {
                filteredOutput << line << "\n";
            }
        }
    }
    return filteredOutput.str();
}



std::vector<std::vector<Producto>> obtenerProductosPorMeses(ssh_session session, const std::string& filePath, const std::vector<std::string>& filtrosFecha) {
    std::vector<std::vector<Producto>> productosPorMeses;
    for (const std::string& filtroFecha : filtrosFecha) {
        std::string command = "grep '" + filtroFecha + "' " + filePath;
        std::string commandOutput;
        int rc = ejecutarcomando(session, command, commandOutput);
        std::string productosFiltrados = filtrarPorEstados(commandOutput, {"FINALIZED", "AUTHORIZED"});

        std::istringstream iss(productosFiltrados);
        std::unordered_map<std::string, std::pair<int, std::pair<std::string, std::string>>> productInfo;  // SKU -> (cantidad, (nombre, precio))
        std::string line;
        while (std::getline(iss, line)) {
            std::istringstream lineIss(line);
            std::string sku, nombre, montoStr;
            if (std::getline(lineIss, sku, ';') &&
                std::getline(lineIss, nombre, ';') &&
                std::getline(lineIss, montoStr, ';')) {
                if (productInfo.find(sku) == productInfo.end()) {
                    productInfo[sku] = std::make_pair(1, std::make_pair(nombre, montoStr));
                } else {
                    productInfo[sku].first++;
                }
            }
        }

        std::vector<Producto> productos;
        for (const auto& pair : productInfo) {
            const std::string& sku = pair.first;
            const int& cantidad = pair.second.first;
            const std::string& nombre = pair.second.second.first;
            const std::string& precio = pair.second.second.second;
            Producto producto;
            producto.SKU = sku;
            producto.nombre = nombre;
            producto.cantidad = cantidad;
            producto.precio = precio;
            productos.push_back(producto);
        }
        productosPorMeses.push_back(productos);
    }
    return productosPorMeses;
}

std::vector<Producto> obtenerProductosRepetidos(const std::vector<std::vector<Producto>>& productosPorMeses) {
    std::vector<Producto> productosRepetidos;
    if (productosPorMeses.size() > 0) {
        const std::vector<Producto>& productosPrimerMes = productosPorMeses[0];
        for (const Producto& producto : productosPrimerMes) {
            bool seRepite = true;
            for (size_t i = 1; i < productosPorMeses.size(); i++) {
                const std::vector<Producto>& productosMes = productosPorMeses[i];
                auto it = std::find_if(productosMes.begin(), productosMes.end(),
                    [&producto](const Producto& p) { return p.SKU == producto.SKU; });
                if (it == productosMes.end()) {
                    seRepite = false;
                    break;
                }
            }
            if (seRepite) {
                productosRepetidos.push_back(producto);
            }
        }
    }
    return productosRepetidos;
}

void calcularCanastaPorMes(const std::vector<Producto>& productosRepetidos, const std::vector<std::vector<Producto>>& productosPorMeses) {
    if (productosPorMeses.empty()) {
        std::cout << "No hay datos disponibles para calcular la inflación." << std::endl;
        return;
    }

    const std::vector<Producto>& productosMesBase = productosPorMeses[0];  // Primer mes como canasta base
    int totalCanastaBase = 0;
    double inflacionAcumulada = 0.0;
    double ipcBase = 100.0;  // IPC base inicializado en 100

    // Calcular el total de la canasta del primer mes (canasta base)
    for (const Producto& producto : productosRepetidos) {
        auto it = std::find_if(productosMesBase.begin(), productosMesBase.end(),
            [&producto](const Producto& p) { return p.SKU == producto.SKU; });

        if (it != productosMesBase.end()) {
            std::string sincomillas = it->precio.substr(1, it->precio.length() - 2); // eliminar comillas y asignar a nueva variable
            int precio = std::stoi(sincomillas);  // Convertir la cadena a entero
            totalCanastaBase += precio;
        }
    }

    // Calcular la inflación por cada mes
    for (size_t i = 1; i < productosPorMeses.size(); ++i) {
        const std::vector<Producto>& productosMes = productosPorMeses[i];
        int totalCanasta = 0;

        // Calcular el total de la canasta del mes actual
        for (const Producto& producto : productosRepetidos) {
            auto it = std::find_if(productosMes.begin(), productosMes.end(),
                [&producto](const Producto& p) { return p.SKU == producto.SKU; });

            if (it != productosMes.end()) {
                std::string sincomillas = it->precio.substr(1, it->precio.length() - 2); // eliminar comillas y asignar a nueva variable
                int precio = std::stoi(sincomillas);  // Convertir la cadena a entero
                totalCanasta += precio;
            }
        }
        // Calcular el IPC del mes actual
        double ipc = (totalCanasta / static_cast<double>(totalCanastaBase)) * 100.0;
        // Calcular la inflación respecto al IPC base
        double inflacion = ((ipc - ipcBase) / ipcBase) * 100.0;
        // Actualizar el IPC base
        ipcBase = ipc;
        // Calcular la inflación acumulada
        inflacionAcumulada += inflacion;
        
        
    }
    std::cout << std::fixed << std::setprecision(4);  // Establecer el formato de impresión
        std::cout << "Inflación acumulada: " << inflacionAcumulada << "%" << std::endl;
}
int main() {
    SSHConnection sshConnection;
    sshConnection.session = ssh_new();
    if (sshConnection.session == nullptr) {
        std::cerr << "Error al crear la sesión SSH." << std::endl;
        return 1;
    }
    const std::string host = "146.190.172.171";
    const std::string port = "22";
    const std::string username = "grupob";
    const std::string password = "ZZ9,upm.MU";
    const std::string remotePath = "/srv/utem/supermercado.csv";
     std::vector<std::string> states = {"FINALIZED", "AUTHORIZED"};
    // Conectar al servidor SSH
    int conectar = connect_ssh(sshConnection.session, host, port, username, password);
    if (conectar != 0) {
        ssh_free(sshConnection.session);
        return 1;
    }
    // Abrir el canal SSH
    conectar = open_ssh_channel(sshConnection.session, sshConnection.channel);
    if (conectar != 0) {
        ssh_disconnect(sshConnection.session);
        ssh_free(sshConnection.session);
        return 1;
    }
std::vector<std::string> filtrosFecha = {
    "2022-01", "2022-02", "2022-03", "2022-04", "2022-05", "2022-06",
    "2022-07", "2022-08", "2022-09", "2022-10", "2022-11", "2022-12"
};
std::vector<std::vector<Producto>> productosPorMeses = obtenerProductosPorMeses(sshConnection.session, remotePath, filtrosFecha);
std::vector<Producto> productosRepetidos = obtenerProductosRepetidos(productosPorMeses);
      calcularCanastaPorMes(productosRepetidos, productosPorMeses);
    // Cerrar el canal SSH
    close_ssh_channel(sshConnection.channel);
    // Cerrar la conexión SSH y liberar los recursos
    ssh_disconnect(sshConnection.session);
    ssh_free(sshConnection.session);

    return conectar;
}
