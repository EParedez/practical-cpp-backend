# Arquitectura del Proyecto

Este documento describe la arquitectura de **PracticalCppBackend**, el backend en C++
reconstruido a partir de los snippets del libro *Practical C++ Backend Programming*
(Justin Barbara, GitforGits 2023). Complementa el `README.md` con el detalle de diseño:
componentes, flujos de datos, decisiones técnicas y el camino a seguir.

---

## 1. Visión general

El proyecto es un **backend de blog** en C++17 con dos superficies de API:

- **REST/HTTP** (puerto `8080`) — para clientes web / Nginx.
- **gRPC** (puerto `50051`) — para servicios internos (microservicios).

Ambas comparten la misma **capa de negocio y de datos** (MongoDB), de modo que el
código de CRUD se escribe una sola vez y se expone por ambos canales.

```
                    ┌──────────────────────────────────────────────┐
   Navegador ──► Nginx ──► HTTP Server (8080)                       │
                    │          │                                    │
                    │          ▼                                    │
   Cliente gRPC ──► │   gRPC Server (50051) ──► BlogServiceImpl     │
                    │                                   │           │
                    │                                   ▼           │
                    │                        BlogRepository (blogcore)
                    │                                   │           │
                    │                                   ▼           │
                    └────────────────────────► MongoDB (27017)      │
                                                                   └────
```

## 2. Componentes y dependencias

| Componente | Ubicación | Responsabilidad | Dependencias |
|------------|-----------|-----------------|--------------|
| `blogcore` (lib estática) | `src/db`, `src/cache`, `src/model` | Modelos, acceso a MongoDB, cachés | `mongo::mongocxx_shared` |
| `blogservice` (lib) | `src/api` + código generado | Implementación de los RPC gRPC | `blogcore`, `gRPC::grpc++`, `protobuf` |
| `blog_http_server` | `src/server/http_server_main.cpp` | API REST sobre MongoDB | `blogcore`, `blogservice`, cpp-httplib |
| `blog_grpc_server` | `src/api/grpc_server_main.cpp` | Servidor gRPC en `0.0.0.0:50051` | `blogservice` |
| `blog_grpc_client` | `src/api/grpc_client_main.cpp` | Cliente de prueba (CRUD completo) | `blogservice` |
| `run_tests` | `tests/` | 24 tests unitarios e integración | `blogcore`, `blogservice`, `GTest` |

### Jerarquía de capas (flujo de una petición)

1. **Transporte** — HTTP (`httplib`) o gRPC (`grpcpp`).
2. **API layer** — `BlogServiceImpl` valida entrada y traduce mensajes ↔ modelos.
3. **Datos** — `BlogRepository` encapsula el driver de MongoDB (pool de conexiones).
4. **Modelo** — `model::User`, `model::Post`, `model::Comment` (structs planos).
5. **Caché** — `LRUCache`/`LFUCache`/`RRCache` (evolución de políticas; se usa LRU en el servicio).

## 3. Capa de datos (MongoDB)

`BlogRepository` (`src/db/blog_repository.{h,cpp}`) es la única clase que toca el driver.

- **Conexión**: `mongocxx::pool` con `mongocxx::uri`, inicializado tras el singleton
  `mongocxx::instance` (requisito del driver 4.x).
- **Colecciones**: `users` y `posts` en la base `blog`.
- **CRUD**: `insert_one` / `find_one` / `update_one` / `delete_one`.
- **Agregación**: `CountPostsPerAuthor()` usa `pipeline.group({_id: "$author", count: {$sum: 1}})`.
- **Índices**: documentado para `users.username` (ver `README.md`); crearlos con
  `collection.create_index(...)`.

> Nota de compatibilidad: el driver instalado es la serie 4.x, cuya API difiere de la
> del libro (3.x): `element::get_utf8()` pasó a `get_string()`, y el stream builder se
> sustituyó por el `basic::make_document`/`kvp`. El repositorio usa la API moderna.

## 4. API gRPC

Contrato en `proto/blog_service.proto`:

```
service BlogService {
  rpc AddPost(Post)          returns (PostResponse);
  rpc GetPost(PostResponse)  returns (FullPostResponse);
  rpc UpdatePost(Post)       returns (PostResponse);
  rpc DeletePost(PostResponse) returns (PostResponse);
  rpc GetAllPosts(PostResponse) returns (AllPostsResponse);
}
```

- **Generación**: CMake invoca `protoc` (+ plugin `grpc_cpp_plugin`) en tiempo de build
  hacia `build/proto/`.
- **Validación**: `AddPost` rechaza título/autor vacíos con `INVALID_ARGUMENT`; las
  operaciones sobre IDs inexistentes devuelven `NOT_FOUND`; fallos de BD, `INTERNAL`.

## 5. Caché (capítulo 7)

Tres políticas en `src/cache/`, todas O(1) en `get`/`put`:

| Clase | Estructuras | Política de desalojo |
|-------|-------------|----------------------|
| `LRUCache` | `unordered_map` + `list` | Least Recently Used |
| `LFUCache` | 3 mapas (`m`, `freq`, `iter`) | Least Frequently Used |
| `RRCache` | `unordered_map` + `vector` | Random Replacement |

`BlogServiceImpl` usa un `LRUCache(128)` como caché de lectura. El diseño permite
intercambiar la política sin tocar la capa de API.

## 6. API REST (HTTP)

`blog_http_server` (`src/server/http_server_main.cpp`) usa cpp-httplib (header-only,
incluido en el repo).

| Método | Ruta | Acción |
|--------|------|--------|
| GET | `/health` | health check |
| GET | `/posts` | lista posts |
| POST | `/posts` | crea post (`title`, `author`, `content`) |
| GET | `/posts/:id` | obtiene post |
| DELETE | `/posts/:id` | elimina post |
| GET | `/stats/posts-per-author` | agregación `$group/$sum` |

## 7. Edge y despliegue

- **Nginx** (`deploy/nginx/`): reverse proxy → `localhost:8080`; load balancer
  round-robin/least_conn/ip_hash; HTTPS con headers de seguridad y rate limiting.
- **Docker** (`Dockerfile` multi-stage + `docker-compose.yml`): mongo + http + grpc + nginx.
- **AWS** (`deploy/aws/beanstalk.config` + `Procfile`): Elastic Beanstalk arranca
  `blog_http_server` en el puerto 5000.
- **CI/CD** (`.github/workflows/`): `test.yml` (build+tests con servicio Mongo) y
  `docker.yml` (push de imagen).

## 8. Build y tests

- CMake con `CMAKE_CXX_STANDARD=17`; targets: `blogcore`, `blogservice`,
  `blog_grpc_server`, `blog_grpc_client`, `blog_http_server`, `run_tests`.
- 24 tests Google Test: 12 de caché (unitarios), 6 de repositorio (integración Mongo),
  6 de gRPC↔MongoDB (servidor real en proceso).
- En macOS ARM bajo Rosetta 2, compilar con `arch -arm64 ...`.

---

# Next Steps

Camino sugerido para evolucionar el proyecto, ordenado por impacto/valor.

## Prioridad alta

- [ ] **Autenticación y autorización** (cap. 10 del libro)
  - Implementar JWT/OAuth y un `AuthInterceptor` gRPC (los snippets ya están en el libro).
  - Roles `writer` / `reader` en MongoDB con `db.createUser`.
  - Endpoint `POST /auth/login` en el HTTP server.
- [ ] **Integrar la caché de verdad en `GetPost`**
  - `LRUCache` ya existe y se instancia; conectar los hits/misses al flujo real y
    añadir `Cache-Control`/`ETag` en HTTP.
- [ ] **Tests de la capa HTTP**
  - Falta `http_server_test.cpp` (hoy solo se testea gRPC y el repo).
- [ ] **Crear índices reales**
  - `create_index` en `users.username` y `posts.author` al arrancar.

## Prioridad media

- [ ] **Configuración por variables de entorno**
  - `MONGODB_URI`, `PORT`, `GRPC_PORT`, capacidad de caché.
- [ ] **Logging estructurado**
  - Logs JSON (spdlog o glog) con request-id y tiempos de respuesta.
- [ ] **Dockerfile de producción pulido**
  - Build estático/`-static` o imagen distroless; probar `docker compose up`.
- [ ] **gRPC con TLS**
  - `grpc::SslServerCredentials` con certificados (generar con `openssl`).
- [ ] **Paginación y búsqueda**
  - `GetAllPosts` con `limit`/`offset` y filtro por `tag`/`author`.

## Prioridad baja / exploración

- [ ] **Microservicio independiente para caché** (cap. 7: "Usando gRPC para cache")
  - Servicio de caché gRPC separado, escalable de forma independiente.
- [ ] **Índice de tests con cobertura**
  - Integrar `gcovr`/Coveralls y reporte de cobertura en CI.
- [ ] **Despliegue real en AWS**
  - Provisionar Mongo Atlas + Elastic Beanstalk; documentar el flujo completo.
- [ ] **Benchmarks de rendimiento**
  - Medir latencia gRPC vs HTTP y los tres algoritmos de caché.
- [ ] **Soporte C++20**
  - Evaluar `std::ranges`/coroutines (el libro menciona estas features).

## Ráfaga de inicio (si retomas el código)

```bash
cd ~/Desktop/PracticalCppBackend
./scripts/start_mongo.sh
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/grpc;/opt/homebrew/opt/mongo-cxx-driver;/opt/homebrew/opt/bsoncxx;/opt/homebrew/opt/mongo-c-driver;/opt/homebrew/opt/googletest"
arch -arm64 cmake --build build -j8          # solo si estás bajo Rosetta
./build/tests/run_tests
./build/blog_http_server 0.0.0.0 8080 &       # API REST
./build/blog_grpc_server 0.0.0.0:50051 &      # API gRPC
./build/blog_grpc_client localhost:50051      # prueba el flujo completo
```
