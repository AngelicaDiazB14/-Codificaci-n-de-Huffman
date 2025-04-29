#!/bin/bash

# Preguntar si desea actualizar el sistema
read -p "¿Desea actualizar los paquetes del sistema? Opcional  (y/n): " actualizar

if [[ "$actualizar" == "Y" || "$actualizar" == "y" ]]; then
    echo "Actualizando paquetes..."
    sudo dnf update -y
    echo "Paquetes actualizados."
else
    echo "Se omitió la actualización de paquetes."
fi

echo "📦 Instalando gcc &  make ."
sudo dnf install -y gcc make
echo "Instalación completada."
make all
echo "Todos los archivos del proyecto han sido compilados."
