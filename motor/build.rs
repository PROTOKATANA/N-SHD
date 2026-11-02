//! Genera la frontera con la GPU y deja a mano lo que necesita CMake para compilarla.
//!
//! `bridges` sin `compile`: el C++ se compila una sola vez, en quien integra `nucleo/`.

use std::path::{Path, PathBuf};
use std::{env, fs, io};

/// Una frontera por cada familia de shaders: ninguna ve lo que no usa.
const PUENTES: [&str; 2] = ["agujero", "nodos"];

fn main() -> io::Result<()> {
    let _ = cxx_build::bridges(PUENTES.map(|p| format!("src/{p}/puente.rs")));
    for puente in PUENTES {
        println!("cargo:rerun-if-changed=src/{puente}/puente.rs");
    }
    exportar()
}

/// `motor/cxx`, o donde diga `SHD_CXX`: las cabeceras y fuentes generadas y `cxx.cc`, en un sitio
/// fijo para CMake. La carpeta se ignora a sí misma.
fn exportar() -> io::Result<()> {
    println!("cargo:rerun-if-env-changed=SHD_CXX");
    let destino = match env::var_os("SHD_CXX") {
        Some(ruta) => PathBuf::from(ruta),
        None => PathBuf::from(var("CARGO_MANIFEST_DIR")?).join("cxx"),
    };
    let generado = PathBuf::from(var("OUT_DIR")?).join("cxxbridge");
    for puente in PUENTES {
        let cabecera = format!("shd/src/{puente}/puente.rs.h");
        copiar(&generado.join("include").join(&cabecera), &destino.join(&cabecera))?;
        copiar(
            &generado.join(format!("sources/shd/src/{puente}/puente.rs.cc")),
            &destino.join(format!("{puente}/puente.rs.cc")),
        )?;
    }
    let cabecera = PathBuf::from(var("DEP_CXXBRIDGE1_HEADER")?);
    // DEP_CXXBRIDGE1_HEADER es <crate cxx>/include/cxx.h
    let raiz = cabecera
        .ancestors()
        .nth(2)
        .ok_or_else(|| io::Error::other("cxx.h sin crate cxx"))?;
    copiar(&cabecera, &destino.join("rust/cxx.h"))?;
    // `cxx.cc` incluye `../include/cxx.h`: se conserva esa estructura
    copiar(&raiz.join("src/cxx.cc"), &destino.join("src/cxx.cc"))?;
    copiar(&cabecera, &destino.join("include/cxx.h"))?;
    escribir(&destino.join(".gitignore"), b"*\n")
}

fn var(nombre: &str) -> io::Result<String> {
    env::var(nombre).map_err(|e| io::Error::other(format!("{nombre}: {e}")))
}

/// Los archivos generados pueden ser enlaces simbólicos: se lee lo que apuntan.
fn copiar(origen: &Path, destino: &Path) -> io::Result<()> {
    let contenido = fs::read(origen).map_err(|e| io::Error::other(format!("{}: {e}", origen.display())))?;
    escribir(destino, &contenido)
}

/// Solo reescribe lo que cambió: si no, CMake lo recompilaría todo cada vez.
fn escribir(destino: &Path, contenido: &[u8]) -> io::Result<()> {
    if fs::read(destino).is_ok_and(|actual| actual == contenido) {
        return Ok(());
    }
    if let Some(carpeta) = destino.parent() {
        fs::create_dir_all(carpeta)?;
    }
    fs::write(destino, contenido)
}
