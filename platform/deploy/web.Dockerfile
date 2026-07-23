FROM node:22.12.0-alpine AS build
WORKDIR /app
COPY platform/web/package.json platform/web/package-lock.json platform/web/tsconfig.json platform/web/vite.config.ts platform/web/index.html ./
COPY platform/web/src ./src
RUN npm ci --ignore-scripts && npm run build

FROM nginx:1.27.3-alpine
COPY platform/deploy/web.nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /app/dist /usr/share/nginx/html
EXPOSE 80
