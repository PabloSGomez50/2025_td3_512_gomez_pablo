import Link from "next/link"
import { Phone, Mail, MapPin, Facebook, Twitter, Instagram, ExternalLink, Github } from "lucide-react"
import SocialIcons from "@/components/layout/social-icons"

export default function Footer() {
  return (
    <footer className="bg-[#008591] text-white">
      <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 py-12">
        <div className="grid grid-cols-1 md:grid-cols-2 gap-8">
          <div>
            <h3 className="text-xl font-bold mb-4">INFORMACIÓN TRABAJO</h3>
            <ul className="space-y-3">
              <li className="flex items-start">
                <Phone className="h-5 w-5 mr-2 mt-0.5 flex-shrink-0" />
                <span>Carga Electrónica Programable</span>
              </li>
              <li className="flex items-start">
                <MapPin className="h-5 w-5 mr-2 mt-0.5 flex-shrink-0" />
                <span>Catedra: Tecnicas Digitales III</span>
              </li>
              <li className="flex items-start">
                <Mail className="h-5 w-5 mr-2 mt-0.5 flex-shrink-0" />
                <span>td3utnfra@gmail.com</span>
              </li>
            </ul>
          </div>

          <div>
            <h3 className="text-xl font-bold mb-4">INFORMACION DEL AUTOR</h3>
            <ul className="space-y-3">
              <li className="flex items-start">
                <Phone className="h-5 w-5 mr-2 mt-0.5 flex-shrink-0" />
                <span>+54 11 4201 4133</span>
              </li>
              <li className="flex items-start">
                <Mail className="h-5 w-5 mr-2 mt-0.5 flex-shrink-0" />
                <span>pablosgomez50@gmail.com</span>
              </li>
              <li className="flex items-start">
                <Github className="h-5 w-5 mr-2 mt-0.5 flex-shrink-0" />
                <span>Repositorio</span>
              </li>
            </ul>
          </div>
          
        </div>

        <div className="mt-12 pt-8 border-t border-[#00727c] text-sm text-center">
          <p>Copyright © 2025. Todos los derechos reservados UTN-FRA</p>
        </div>
      </div>
    </footer>
  )
}