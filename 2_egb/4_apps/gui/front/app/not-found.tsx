export default function NotFound() {
  return (
    <div className="flex flex-col items-center justify-center min-h-[60vh] py-16">
      <h1 className="text-2xl font-bold mb-4">404 - Página no encontrada</h1>
      <p className="text-muted-foreground mb-6">La página que buscas no existe.</p>
      <a
        href="/"
        className="inline-block rounded-md bg-primary px-6 py-2 text-primary-foreground font-medium shadow hover:bg-primary/90 transition-colors"
      >
        Volver al inicio
      </a>
    </div>
  )
}