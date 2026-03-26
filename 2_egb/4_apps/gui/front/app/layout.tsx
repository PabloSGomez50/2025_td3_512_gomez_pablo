import type { Metadata } from "next";
import { Inter  } from "next/font/google";
import "./globals.css";
import { ThemeProvider } from "@/components/theme-provider"
import Header from "@/components/layout/header"
import Footer from "@/components/layout/footer"

import { TooltipProvider } from "@/components/ui/tooltip"
import { EgbProvider } from "@/hooks/useEgbContext";
import { Toaster } from "@/components/ui/toaster"

const inter = Inter({ subsets: ["latin"] })
// UTN Page font:
// -apple-system,BlinkMacSystemFont,Segoe UI,Roboto,Oxygen-Sans,Ubuntu,Cantarell,Helvetica Neue,sans-serif

export const metadata: Metadata = {
  title: "Carga electronica - Pablo Gomez",
  description: "Proyecto de Carga Electronica Programable para la catedra de Tecnicas Digitales III de la UTN-FRA",
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="es" suppressHydrationWarning>
      <head>
        <meta charSet="UTF-8" />
        <title>{String(metadata.title)}</title>
      </head>
      <body
        className={`${inter.className} antialiased min-h-screen`}
      >
        <TooltipProvider>
          <EgbProvider>
            <ThemeProvider attribute="class" defaultTheme="light" enableSystem>
              <Header />
              <main className="min-h-[75dvh]">{children}</main>
              <Footer />
            </ThemeProvider>
          </EgbProvider>
        </TooltipProvider>
        <Toaster />
      </body>
    </html>
  );
}
