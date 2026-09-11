# Practical C++ Backend Programming

Proyecto completo de backend en C++ construido a partir de los snippets del libro
**"Practical C++ Backend Programming"** (Justin Barbara, GitforGits 2023).
El libro solo ofrece fragmentos de código por capítulo; este repositorio los integra
en una aplicación real, compilable y testeada: **un servidor de blog**.

> Traducción conservada de la documentación original. La documentación activa está
> en inglés en el [README principal](../../README.md).
>
> Documentos: **[Arquitectura y Next Steps](ARCHITECTURE.md)** · [README](#practical-c-backend-programming)
>
> English documentation: [Improvement Plan](../../IMPROVEMENT_PLAN.md) ·
> [HTTP API](../../API.md) · [Configuration](../../CONFIGURATION.md) ·
> [Persistence](../../PERSISTENCE.md) · [Cache](../../CACHE.md) ·
> [Security](../../SECURITY.md) · [Local Docker Deployment](../../DEPLOYMENT.md) ·
> [Testing Guide](../../TESTING.md) · [Release and Rollback](../../RELEASE.md)

## Stack (según los capítulos del libro)

| Capítulo | Tema | Implementación |
|----------|------|----------------|
| 5 | Bases de datos | **MongoDB** vía `mongo-cxx-driver` (CRUD, agregación, índices) |
| 6 | APIs | **gRPC** + Protocol Buffers (`proto/blog_service.proto`) |
| 7 | Caché | **LRU**, **LFU** y **RR** en STL |
| 8 | Servidor web | **HTTP** server (cpp-httplib) + **Nginx** reverse proxy / load balancer |
| 9 | Testing | **Google Test**: unit tests + integration tests (MongoDB + gRPC) |
| 10 | Seguridad | TLS, auth tokens, cabeceras HTTP, rate limiting |
| 11 | Despliegue | **Docker**, **GitHub Actions**, **AWS ECS Fargate** |

## Estructura

```
PracticalCppBackend/
├── CMakeLists.txt              # build principal
├── Dockerfile                  # build multi-stage
├── docker-compose.yml          # mongo + http + grpc + nginx
├── proto/
│   └── blog_service.proto      # contrato gRPC CRUD + listado filtrado
├── src/
│   ├── model/blog_models.h     # User, Post, Comment
│   ├── auth/auth.{h,cpp}       # tokens por rol, PBKDF2 y rate limiting
│   ├── db/blog_repository.{h,cpp}      # capa MongoDB (CRUD + agregación)
│   ├── cache/lru_cache.{h,cpp}         # LRU O(1)
│   ├── cache/lfu_cache.{h,cpp}         # LFU
│   ├── cache/rr_cache.{h,cpp}          # Random Replacement
│   ├── api/blog_service_impl.{h,cpp}   # implementación gRPC
│   ├── api/grpc_server_main.cpp        # servidor gRPC (0.0.0.0:50051)
│   ├── api/grpc_client_main.cpp        # cliente de prueba
│   └── server/http_server_main.cpp     # servidor HTTP (0.0.0.0:8080)
├── tests/
│   ├── cache_test.cpp          # unit tests LRU/LFU/RR
│   ├── repository_test.cpp     # integration tests MongoDB
│   ├── integration_test.cpp    # integration tests gRPC + MongoDB + auth
│   ├── security_test.cpp       # unit tests de autenticación y PBKDF2
│   └── CMakeLists.txt
├── deploy/
│   ├── nginx/                  # reverse proxy, load balancer, HTTPS + seguridad
│   └── aws/                    # ECS Fargate task definitions
└── .github/workflows/          # CI (test) y CD (Docker)
```

## Requisitos

- CMake ≥ 3.16
- Compilador C++17
- `mongo-cxx-driver` (v4.x), `grpc` (con `grpc_cpp_plugin`), `googletest`
- MongoDB server (para tests de integración)

### macOS (Homebrew)

```bash
brew install mongo-cxx-driver grpc googletest
brew tap mongodb/brew && brew install mongodb-community@8.0
```

## Compilar y testear

```bash
./scripts/setup.sh          # instala deps, arranca Mongo, compila y corre tests
# o manualmente:
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/grpc;/opt/homebrew/opt/mongo-cxx-driver;/opt/homebrew/opt/bsoncxx;/opt/homebrew/opt/mongo-c-driver;/opt/homebrew/opt/googletest"
cmake --build build -j8
ctest --test-dir build --output-on-failure  # 73 tests
```

Nota: si el shell corre bajo Rosetta 2 en un Mac ARM, anteponga `arch -arm64` a los
comandos de build para evitar errores de arquitectura.

## Ejecutar

```bash
./scripts/start_mongo.sh                 # Mongo en localhost:27017

# API HTTP (el backend del blog)
./build/blog_http_server 0.0.0.0 8080

# API gRPC
./build/blog_grpc_server 0.0.0.0:50051
./build/blog_grpc_client localhost:50051   # cliente de prueba (CRUD completo)
```

### Endpoints HTTP

| Método | Ruta | Descripción |
|--------|------|-------------|
| GET | `/health` | health check |
| GET | `/ready` | database-aware readiness check |
| GET | `/metrics` | Prometheus metrics |
| GET | `/posts` | lista posts |
| POST | `/posts` | crea post (`title`, `author`, `content`) |
| GET | `/posts/:id` | obtiene post |
| PUT | `/posts/:id` | reemplaza post |
| DELETE | `/posts/:id` | elimina post |
| GET | `/stats/posts-per-author` | agregación MongoDB `$group/$sum` |

## Nginx

```bash
nginx -t -c deploy/nginx/nginx.conf            # validar sintaxis
sudo cp deploy/nginx/nginx.conf /etc/nginx/conf.d/default.conf
sudo nginx -s reload
```

Incluye configs para reverse proxy, load balancing (round robin / least_conn / ip_hash)
y HTTPS con rate limiting y cabeceras de seguridad.

## Docker

```bash
docker compose up --build
# HTTP:  http://localhost:8080   (vía Nginx: http://localhost/)
# gRPC:  localhost:50051

# Verificación aislada de toda la pila y del flujo CRUD
./scripts/smoke_test.sh
```

## AWS (ECS Fargate)

El modelo soportado usa servicios ECS separados para HTTP y gRPC, una imagen GHCR
inmutable y secretos inyectados desde AWS Secrets Manager. Consulte
[Release and Rollback](../../RELEASE.md) y las plantillas en `../../deploy/aws/`.

## CI/CD

- `.github/workflows/test.yml` — formato, build estricto, tests unitarios/de
  integración, sanitizers, análisis estático, cobertura, scans y smoke test.
- `.github/workflows/docker.yml` — publica en GHCR solo después de CI exitoso, con
  tag inmutable, SBOM, scan de imagen y attestation de procedencia.

## Nota sobre el libro

Este proyecto reconstruye el backend que el libro describe solo a través de snippets
(el repositorio oficial del libro no se publica en el PDF). Los nombres de clases,
`.proto` y operaciones siguen fielmente los ejemplos de los capítulos 5–11.
