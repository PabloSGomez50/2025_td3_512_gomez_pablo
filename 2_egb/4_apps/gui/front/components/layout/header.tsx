"use client"

import { useEffect, useState } from "react"
import Link from "next/link"
import Image from "next/image"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Tooltip, TooltipTrigger, TooltipContent } from "@/components/ui/tooltip"
import { Facebook, Twitter, Instagram, Linkedin, Search, Menu, X, ChevronDown, Sun, Moon } from "lucide-react"

import { usePathname } from "next/navigation"
import SocialIcons from "./social-icons"
import { useTheme } from "next-themes"



export default function Header() {
  const [isMenuOpen, setIsMenuOpen] = useState(false)
  const { setTheme, theme } = useTheme()

  const pathname = usePathname();

  const scrollbarHideStyles = `
  .scrollbar-hide::-webkit-scrollbar {
    display: none;
  }
  .scrollbar-hide {
    -ms-overflow-style: none;
    scrollbar-width: none;
  }
`

  const navLinks = [
    { href: '/', label: 'INICIO' },
    { href: '/websocket', label: 'WEBSOCKET'},
    { href: '/faq', label: 'FAQ' },
  ];

  
  const handleThemeChange = () => {
    if (!theme) return
    if (theme == "light") {
      setTheme("dark")
      localStorage.setItem("theme_preference", "dark")
    } else {
      setTheme("light")
      localStorage.setItem("theme_preference", "light")
    }
  }

  useEffect(() => {
    const theme_preference = localStorage.getItem("theme_preference");
    if (theme_preference) {
      setTheme(theme_preference);
    }
  }, [])

  return (
    <>
      <style jsx global>
        {scrollbarHideStyles}
      </style>
      <header className="w-full sticky top-0 z-50 bg-background/95 shadow-sm">
        <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8">
          <div className="flex justify-between items-center py-4">
            <div className="flex items-center">
              <Link href="/" className="flex-shrink-0">
                <Image
                  src="/Logo-UTN-Header.webp"
                  alt="UTN Avellaneda Logo"
                  width={200}
                  height={60}
                  className="h-12 w-auto"
                />
              </Link>
            </div>

            <h2 className="text-xl font-semibold">Carga electronica - EGB</h2>

            <div className="hidden md:flex items-center gap-6">
              {/* <Button asChild>
                <Link href="https://www.utnfravirtual.org.ar/" className="h-2">
                  Campus virtual
                </Link>
              </Button>
              <div className="relative w-64 border-b-2 border-ring rounded">
                <Input type="text" placeholder="Buscar..." className="pr-10" />
                <Search className="absolute right-3 top-1/2 transform -translate-y-1/2 h-4 w-4 text-gray-400" />
              </div> */}
              {/* Toggle de tema con tooltip */}
              <Tooltip>
                <TooltipTrigger asChild>
                  <Button
                    variant="ghost"
                    size="icon"
                    className="h-9 w-9 cursor-pointer"
                    onClick={handleThemeChange}
                    aria-label="Cambiar tema"
                  >
                    <Sun className="h-4 w-4 rotate-0 scale-100 transition-all dark:-rotate-90 dark:scale-0" />
                    <Moon className="absolute h-4 w-4 rotate-90 scale-0 transition-all dark:rotate-0 dark:scale-100" />
                  </Button>
                </TooltipTrigger>
                <TooltipContent>
                  Cambiar tema
                </TooltipContent>
              </Tooltip>
              {/* <SocialIcons /> */}
            </div>

            <button className="md:hidden" onClick={() => setIsMenuOpen(!isMenuOpen)}>
              {isMenuOpen ? <X size={24} /> : <Menu size={24} />}
            </button>
          </div>

          {/* <nav
            className="md:block py-3 relative hidden"
          >
            <div className="flex items-center scrollbar-hide">
              <ul className="flex space-x-6 min-w-max">
                {navLinks.map((link) => (
                  <li
                    key={link.href}
                  >
                    <Link
                      href={link.href}
                      className={`font-medium pb-2 ${pathname === link.href
                        ? 'text-bold border-b-2 border-secondary/80'
                        : 'text-gray-700 hover:border-b-2 hover:text-secondary/80'
                        }
                        `}
                    >
                      {link.label}
                    </Link>
                  </li>
                ))}
              </ul>
            </div>
          </nav> */}

          {isMenuOpen && (
            <div className="md:hidden py-4">
              <div className="flex flex-col space-y-4 pb-4">
                <div className="relative">
                  <Input type="text" placeholder="Buscar..." className="pr-10" />
                  <Search className="absolute right-3 top-1/2 transform -translate-y-1/2 h-4 w-4 text-gray-400" />
                </div>

                <Button className="bg-teal-600 hover:bg-teal-700 w-full">Registrarse o Iniciar Sesión</Button>

                {/* <SocialIcons className="justify-center"/> */}
              </div>

              <ul className="flex flex-col space-y-2">
                {navLinks.map((link) => (
                  <li key={link.href}>
                    <Link
                      href={link.href}
                      className={`block py-2 ${pathname !== link.href ? 'text-gray-700' : 'text-blue-700'} hover:text-blue-900 font-medium`}
                    >
                      {link.label}
                    </Link>
                  </li>
                ))}
              </ul>
            </div>
          )}
        </div>
      </header>
    </>
  )
}